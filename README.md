# PAYDAY 2 VR – per-eye frustum fix

Fixes eye strain / "crossed eyes" / headaches in **PAYDAY 2 (official VR mode)** on headsets whose two eyes report slightly
different fields of view – confirmed on the **Steam Frame** (streamed through SteamVR).

## The problem

PAYDAY 2 builds both eye cameras from the *union* of the two eyes' SteamVR frusta (max outer, max inner, max top, max bottom),
so both eyes share the same vertical extent. SteamVR, however, maps each eye's texture back onto that eye's **own** frustum.
When the frusta differ per eye, the compositor shifts the two images in opposite directions. On the Steam Frame
(left eye: up 1.160 / down 1.748, right eye: up 1.188 / down 1.699) that is about **2° (~3.6 prism dioptres) of vertical
disparity** at the centre of the view. The picture still looks right – depth, scale and tracking are correct – but your eyes
are pulled in opposite vertical directions, which feels like long-term crossed eyes and ends in a headache.

Headsets with near-identical per-eye frusta never showed it.

## The fix

A small DLL is injected into `PAYDAY2.exe` when the game starts. It replaces the engine's frustum builder with an
equivalent one that is fed each eye's exact frustum (what any normal OpenVR application does). Nothing else changes: IPD, FOV,
lens shift and rendering are untouched. **No game file is modified** – the patch lives in memory only.

## Install & use

1. Download this repository (Code → Download ZIP, or `git clone`) and unzip it anywhere. Keep the files together:
   ```
   PaydayVR_Fixed.exe
   hook.ini
   hook\hook.dll
   ```
2. Start SteamVR / connect your headset as usual, and close PAYDAY 2 if it is running.
3. Pick **one** of the two ways to launch:

   **A) Non-Steam shortcut** – Steam → *Games → Add a Non-Steam Game to My Library → Browse…* → select `PaydayVR_Fixed.exe`.
   Starting that entry launches PAYDAY 2 in VR, applies the fix and stays alive (Steam shows it as "playing") until the game exits.

   **B) Launch option of the real game** – Steam → right-click *PAYDAY 2 → Properties → Launch Options*:
   ```
   "<full path to>\PaydayVR_Fixed.exe" %command%
   ```
   Every launch of the game (including "Launch in VR") then gets the fix. Clear the field to go back to stock.

   Don't combine A and B.

Launching PAYDAY 2 normally from Steam without either method gives the original, unpatched behaviour.

### Settings

`hook.ini`:

| key | meaning |
|---|---|
| `per_eye_frusta` | `1` = apply the fix (default), `0` = original game behaviour |

Logs are written to `logs\launcher.log` and `logs\hook.log` next to the exe – useful when reporting problems.

### Uninstall

Delete the folder (and clear the Launch Option / remove the shortcut). The game itself was never modified.

## Safety, compatibility and limits

- The hook is specific to the current PAYDAY 2 build (Steam build **25506656**). It verifies the code it patches and does
  nothing (the game runs unpatched) if it does not recognise it, e.g. after a game update – open an issue then.
- The fix changes only how the camera frustum is computed on your machine. It touches no gameplay data and no network traffic,
  and the game's own host-side cheat checks look at player/gameplay data.
- It does, however, use DLL injection and an in-memory code patch, the same technique cheats use. The game shows no
  EasyAntiCheat/BattlEye; I could not confirm whether it is protected by VAC. **Use at your own risk.**
- Antivirus software may flag a DLL injector; the full source is in `src\` and you can build it yourself.

## Build from source

Requires the Visual C++ x64 build tools ("Build Tools for Visual Studio", C++ workload). Then run:

```
build.bat
```

It produces `hook\hook.dll` and `PaydayVR_Fixed.exe` (statically linked, no runtime installer needed).

| file | purpose |
|---|---|
| `src\hook.cpp` | the injected DLL: remembers each eye's real frustum and replaces the engine's frustum builder |
| `src\launcher.cpp` | `PaydayVR_Fixed.exe`: launches the game (or wraps Steam's `%command%`) and injects the DLL |
