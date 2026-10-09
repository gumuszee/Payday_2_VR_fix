// PaydayVR_Fixed.exe - launches PAYDAY 2 (VR) with the per-eye frustum fix injected. No console window.
//
//  Mode A (no arguments):  add this exe as a "Non-Steam Game". It asks Steam to launch  steam://launch/218620/VR ,
//                          injects hook\hook.dll into PAYDAY2.exe as soon as it appears, and stays alive until the game exits
//                          (so Steam shows the shortcut as "playing" and its Stop button works on the launcher).
//  Mode B (with arguments): set it as Steam launch option of PAYDAY 2:   "<folder>\PaydayVR_Fixed.exe" %command%
//                          Steam passes the real game command line; we start it, inject, and wait for it to exit.
//
// Nothing is written to the game folder. Log: logs\launcher.log
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <string>
#include <set>
#include <vector>
#include <cstdio>
#include <cstdarg>

static std::wstring g_root;

static void logf(const wchar_t* fmt, ...) {
    CreateDirectoryW((g_root + L"logs").c_str(), nullptr);
    FILE* f = _wfopen((g_root + L"logs\\launcher.log").c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fwprintf(f, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfwprintf(f, fmt, ap); va_end(ap);
    fwprintf(f, L"\n"); fclose(f);
}

static void fail(const wchar_t* msg) { logf(L"ERROR: %s", msg); MessageBoxW(nullptr, msg, L"Payday 2 VR fix", MB_OK | MB_ICONERROR); }

static std::vector<DWORD> gamePids() {
    std::vector<DWORD> v;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) do { if (!_wcsicmp(pe.szExeFile, L"PAYDAY2.exe")) v.push_back(pe.th32ProcessID); } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return v;
}

static bool inject(DWORD pid, const std::wstring& dll) {
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) { logf(L"OpenProcess(%lu) failed %lu", pid, GetLastError()); return false; }
    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    if (mem && WriteProcessMemory(h, mem, dll.c_str(), bytes, nullptr)) {
        auto ll = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HANDLE t = CreateRemoteThread(h, nullptr, 0, ll, mem, 0, nullptr);
        if (t) { WaitForSingleObject(t, 15000); DWORD code = 0; GetExitCodeThread(t, &code); ok = true; logf(L"injected into pid %lu (LoadLibrary low dword 0x%lx)", pid, code); CloseHandle(t); }
        else logf(L"CreateRemoteThread failed %lu", GetLastError());
    }
    CloseHandle(h);
    return ok;
}

static void waitForExit(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
}

// After the game we launched exits, give a short grace period: some builds re-launch themselves through Steam.
static void graceLoop(const std::wstring& dll, std::set<DWORD>& seen) {
    for (int i = 0; i < 300; i++) {   // 15 s
        for (DWORD pid : gamePids()) if (!seen.count(pid)) { seen.insert(pid); logf(L"new game process %lu after exit", pid); if (inject(pid, dll)) { waitForExit(pid); i = 0; } }
        Sleep(50);
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_root = self; g_root.resize(g_root.find_last_of(L'\\') + 1);
    std::wstring dll = g_root + L"hook\\hook.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) { fail(L"hook\\hook.dll not found next to PaydayVR_Fixed.exe."); return 1; }

    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::set<DWORD> seen;
    logf(L"---- start (argc=%d) ----", argc);

    if (argc > 1) {
        // Mode B: run the command Steam gave us (everything after our own exe path in the raw command line)
        const wchar_t* cl = GetCommandLineW();
        if (*cl == L'"') { cl++; while (*cl && *cl != L'"') cl++; if (*cl) cl++; } else { while (*cl && *cl != L' ') cl++; }
        while (*cl == L' ') cl++;
        std::wstring cmd = cl;
        std::wstring dir = argv[1]; size_t sl = dir.find_last_of(L"\\/"); dir = sl == std::wstring::npos ? L"" : dir.substr(0, sl);
        logf(L"mode B: %s", cmd.c_str());
        STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) { fail(L"Could not start the game command passed by Steam."); return 1; }
        seen.insert(pi.dwProcessId);
        inject(pi.dwProcessId, dll);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        graceLoop(dll, seen);
        logf(L"mode B done");
        return 0;
    }

    // Mode A
    if (!gamePids().empty()) { fail(L"PAYDAY 2 is already running. Close it first, then start this shortcut again (the fix must be injected at game start)."); return 1; }
    logf(L"mode A: launching steam://launch/218620/VR");
    ShellExecuteW(nullptr, L"open", L"steam://launch/218620/VR", nullptr, nullptr, SW_SHOWNORMAL);
    ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < 5 * 60 * 1000) {
        for (DWORD pid : gamePids()) {
            if (seen.count(pid)) continue;
            seen.insert(pid);
            inject(pid, dll);
            waitForExit(pid);
            graceLoop(dll, seen);
            logf(L"mode A done");
            return 0;
        }
        Sleep(10);
    }
    fail(L"PAYDAY 2 did not start within 5 minutes. Is Steam running?");
    return 2;
}
