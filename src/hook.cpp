// PAYDAY 2 VR - per-eye frustum fix.
//
// PAYDAY2.exe builds both eye cameras from the UNION of the two eyes' SteamVR frusta (max outer, max inner, max top,
// max bottom), so both eyes share the same vertical extent. SteamVR maps each eye's texture onto that eye's OWN frustum.
// On a headset whose per-eye frusta differ (e.g. Steam Frame) the compositor therefore shifts the two images in opposite
// directions (~2 degrees of vertical disparity), which causes eye strain and headaches.
//
// This DLL is injected into PAYDAY2.exe by PaydayVR_Fixed.exe. It
//   1. patches the exe's import of openvr_api!VR_GetGenericInterface and hooks IVRSystem::GetProjectionRaw to remember the
//      real per-eye frusta, and
//   2. replaces the engine's frustum builder (exe+0x96de60, same maths) so each eye gets its own exact frustum.
// Nothing on disk is modified. If the exe does not match the expected build the patch is skipped (game runs unpatched).
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <mutex>
#include <string>

// ---------------------------------------------------------------------------------------------------------------------
// Offsets inside PAYDAY2.exe (Steam build 25506656)
static const uintptr_t RVA_BUILDER         = 0x96de60;  // frustum builder: (left, right, bottom, top, float* out[4])
static const uint64_t  RVA_CALL_LEFT_EYE   = 0x96fa91;  // return address of the builder call for the left eye
static const uint64_t  RVA_CALL_RIGHT_EYE  = 0x96fab1;  // ... and for the right eye
static const int       VTBL_GETPROJECTIONRAW = 2;       // IVRSystem vtable slot of GetProjectionRaw (stable across versions)

// ---------------------------------------------------------------------------------------------------------------------
static char g_root[MAX_PATH] = "";          // folder that contains hook.ini / logs (parent of hook\), with trailing '/'
static uintptr_t g_exeBase = 0;
static bool g_enabled = true;
static std::mutex g_logMu;
static FILE* g_log = nullptr;

static std::string RootPath(const char* rel) { return std::string(g_root) + rel; }

static void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_log) {
        CreateDirectoryA(RootPath("logs").c_str(), nullptr);
        g_log = fopen(RootPath("logs/hook.log").c_str(), "w");
        if (!g_log) return;
    }
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

// ---------------------------------------------------------------------------------------------------------------------
// Real per-eye frusta as reported by the runtime (OpenVR raw tangents: left, right, top, bottom)
static float g_raw[2][4];
static bool  g_rawValid[2];

typedef void (*GetProjectionRawFn)(void*, int, float*, float*, float*, float*);
static GetProjectionRawFn g_origGetProjectionRaw = nullptr;

static void HookedGetProjectionRaw(void* self, int eye, float* l, float* r, float* t, float* b) {
    g_origGetProjectionRaw(self, eye, l, r, t, b);
    if (eye == 0 || eye == 1) {
        g_raw[eye][0] = *l; g_raw[eye][1] = *r; g_raw[eye][2] = *t; g_raw[eye][3] = *b;
        g_rawValid[eye] = true;
        Log("runtime frustum eye%d: L=%.5f R=%.5f T=%.5f B=%.5f", eye, *l, *r, *t, *b);
    }
}

// Replacement for the engine's frustum builder. Same maths as the original; for the two VR call sites the union
// frustum passed by the game is swapped for the exact frustum of that eye.
static float* HookedBuildProjection(float l, float r, float b, float t, float* out) {
    uint64_t site = (uint64_t)((uintptr_t)_ReturnAddress() - g_exeBase);
    int eye = site == RVA_CALL_LEFT_EYE ? 0 : site == RVA_CALL_RIGHT_EYE ? 1 : -1;
    if (g_enabled && eye >= 0 && g_rawValid[0] && g_rawValid[1]) {
        l = g_raw[eye][0]; r = g_raw[eye][1]; t = g_raw[eye][2]; b = g_raw[eye][3];
        Log("eye%d camera built from its own frustum", eye);
    }
    float w = r - l;
    out[0] = (float)(2.0 * atan((double)(w * 0.5f)));   // horizontal fov
    out[1] = w / (b - t);                                // aspect
    out[2] = (l + r) / (l - r);                          // horizontal lens shift
    out[3] = (b + t) / (t - b);                          // vertical lens shift
    return out;
}

static bool InstallBuilderPatch() {
    BYTE* fn = (BYTE*)g_exeBase + RVA_BUILDER;
    static const BYTE expected[] = { 0x48, 0x83, 0xEC, 0x78, 0x0F, 0x29, 0x74, 0x24, 0x60 };  // sub rsp,78h ; movaps [rsp+60h],xmm6
    // The exe may still be unpacking itself when we are injected, so wait (up to 60 s) for the expected code to appear.
    // A different game build never matches and is left untouched.
    bool match = false;
    for (int i = 0; i < 12000 && !match; i++) {
        match = !IsBadReadPtr(fn, sizeof(expected)) && memcmp(fn, expected, sizeof(expected)) == 0;
        if (!match) Sleep(5);
    }
    if (!match) {
        Log("unexpected code at exe+0x%llx (different game build?) - fix NOT applied. bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x",
            (unsigned long long)RVA_BUILDER, fn[0], fn[1], fn[2], fn[3], fn[4], fn[5], fn[6], fn[7], fn[8]);
        return false;
    }
    BYTE jmp[12] = { 0x48, 0xB8, 0,0,0,0,0,0,0,0, 0xFF, 0xE0 };   // mov rax, imm64 ; jmp rax
    void* target = (void*)&HookedBuildProjection;
    memcpy(jmp + 2, &target, sizeof(target));
    DWORD old;
    if (!VirtualProtect(fn, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(fn, jmp, sizeof(jmp));
    VirtualProtect(fn, sizeof(jmp), old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, sizeof(jmp));
    Log("frustum builder patched");
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
typedef void* (*GetGenericInterfaceFn)(const char*, int*);
static GetGenericInterfaceFn g_origGetGenericInterface = nullptr;

static void* HookedGetGenericInterface(const char* name, int* err) {
    void* itf = g_origGetGenericInterface(name, err);
    if (itf && !strncmp(name, "IVRSystem_", 10) && !g_origGetProjectionRaw) {
        void** vtbl = *(void***)itf;
        DWORD old;
        if (VirtualProtect(&vtbl[VTBL_GETPROJECTIONRAW], sizeof(void*), PAGE_READWRITE, &old)) {
            g_origGetProjectionRaw = (GetProjectionRawFn)vtbl[VTBL_GETPROJECTIONRAW];
            vtbl[VTBL_GETPROJECTIONRAW] = (void*)&HookedGetProjectionRaw;
            VirtualProtect(&vtbl[VTBL_GETPROJECTIONRAW], sizeof(void*), old, &old);
            Log("hooked %s::GetProjectionRaw", name);
        }
    }
    return itf;
}

// Replace the exe's import-table entry for openvr_api.dll!VR_GetGenericInterface
static bool PatchImport() {
    BYTE* base = (BYTE*)g_exeBase;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((char*)(base + imp->Name), "openvr_api.dll") != 0) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (strcmp(((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, "VR_GetGenericInterface") != 0) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            g_origGetGenericInterface = (GetGenericInterfaceFn)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)&HookedGetGenericInterface;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    // <root>\hook\hook.dll  ->  g_root = "<root>/"
    char p[MAX_PATH];
    GetModuleFileNameA(self, p, MAX_PATH);
    for (char* c = p; *c; ++c) if (*c == '\\') *c = '/';
    char* s = strrchr(p, '/'); if (s) *s = 0;   // strip hook.dll
    s = strrchr(p, '/');       if (s) *s = 0;   // strip hook
    snprintf(g_root, sizeof(g_root), "%s/", p);

    g_enabled = GetPrivateProfileIntA("fix", "per_eye_frusta", 1, RootPath("hook.ini").c_str()) != 0;
    g_exeBase = (uintptr_t)GetModuleHandleA(nullptr);
    Log("hook loaded, per_eye_frusta=%d", (int)g_enabled);
    if (g_enabled) {
        if (!PatchImport()) Log("could not find the openvr_api import - fix NOT applied");
        // patch the engine function from a worker thread (it may need to wait; never block under the loader lock)
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD { InstallBuilderPatch(); return 0; }, nullptr, 0, nullptr);
    }
    return TRUE;
}
