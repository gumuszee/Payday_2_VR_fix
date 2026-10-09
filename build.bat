@echo off
rem Builds hook\hook.dll and PaydayVR_Fixed.exe with the MSVC x64 toolchain (Visual Studio or Build Tools, "Desktop development with C++").
rem Run it from any prompt; if the compiler is not on PATH it is located with vswhere.
setlocal
cd /d "%~dp0"

where cl >nul 2>&1
if not errorlevel 1 goto build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto notools
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto notools
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

:build
if not exist build mkdir build
if not exist hook mkdir hook

cl /nologo /O2 /MT /LD /EHsc /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS src\hook.cpp /Fo:build\ /Fe:hook\hook.dll /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE src\launcher.cpp /Fo:build\ /Fe:PaydayVR_Fixed.exe /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS user32.lib shell32.lib
if errorlevel 1 exit /b 1

echo.
echo Built hook\hook.dll and PaydayVR_Fixed.exe
exit /b 0

:notools
echo Could not find the Visual C++ x64 build tools.
echo Install "Build Tools for Visual Studio" (C++ workload) or run this from an "x64 Native Tools Command Prompt".
exit /b 1
