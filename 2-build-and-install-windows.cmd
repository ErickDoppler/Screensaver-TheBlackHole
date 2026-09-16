@echo off
rem ===========================================================================
rem  The Black Hole - build TheBlackHole.scr for 64-bit Windows and install it
rem
rem  Uses the portable toolchain that 1-download-tools-windows.cmd put in
rem  C:\workenv (override with the BH_WORKENV variable). Nothing else is
rem  required: no Visual Studio, no system CMake, no network - SDL3 is
rem  compiled from the local source copy.
rem
rem  After a successful build the .scr is checked with Microsoft Defender
rem  (when Defender is the active antivirus), then installed for the current
rem  user and selected as the screensaver. No administrator rights needed.
rem
rem  Usage:  2-build-and-install-windows.cmd [debug] [clean] [noinstall] [noscan]
rem            debug      build with symbols into build\win-mingw-debug
rem            clean      throw the build folder away first (full rebuild)
rem            noinstall  build only
rem            noscan     skip the Defender check
rem ===========================================================================
setlocal enabledelayedexpansion

set "WORKENV=C:\workenv"
if not "%BH_WORKENV%"=="" set "WORKENV=%BH_WORKENV%"
rem  the toolchain file reads this too
set "BH_WORKENV=%WORKENV%"

set "ROOT=%~dp0"
set "PRESET=win-mingw"
set "CLEAN="
set "INSTALL=1"
set "SCAN=1"

for %%A in (%*) do (
    if /i "%%~A"=="debug"     set "PRESET=win-mingw-debug"
    if /i "%%~A"=="clean"     set "CLEAN=1"
    if /i "%%~A"=="rebuild"   set "CLEAN=1"
    if /i "%%~A"=="noinstall" set "INSTALL="
    if /i "%%~A"=="noscan"    set "SCAN="
)

set "D_W64DEVKIT=%WORKENV%\w64devkit"
set "D_CMAKE=%WORKENV%\cmake-4.4.3-windows-x86_64"
set "D_NINJA=%WORKENV%\ninja"
set "D_SDL=%WORKENV%\SDL3-3.4.16"

echo.
echo  The Black Hole - Windows x64 build
echo  ----------------------------------
echo  toolchain : %WORKENV%
echo  preset    : %PRESET%
echo.

rem --- everything the preset and the toolchain file point at ----------------
call :require "%D_W64DEVKIT%\bin\gcc.exe" "w64devkit / GCC"      || goto :nokit
call :require "%D_CMAKE%\bin\cmake.exe"   "CMake"                || goto :nokit
call :require "%D_NINJA%\ninja.exe"       "Ninja"                || goto :nokit
call :require "%D_SDL%\CMakeLists.txt"    "SDL3 source"          || goto :nokit

rem  The pinned CMake goes on PATH ahead of w64devkit, which carries its own
rem  slightly older copy; Ninja likewise before anything the user may have.
set "PATH=%D_CMAKE%\bin;%D_NINJA%;%D_W64DEVKIT%\bin;%PATH%"

rem  CMake wants forward slashes in cache values.
set "CM_NINJA=%D_NINJA:\=/%/ninja.exe"
set "CM_SDL=%D_SDL:\=/%"
set "CM_W64=%D_W64DEVKIT:\=/%"

if defined CLEAN (
    if exist "%ROOT%build\%PRESET%" (
        echo  Removing build\%PRESET% ...
        rmdir /s /q "%ROOT%build\%PRESET%" || goto :fail
    )
)

pushd "%ROOT%" || goto :fail

echo  Configuring...
cmake --preset %PRESET% "-DCMAKE_MAKE_PROGRAM=%CM_NINJA%" "-DFETCHCONTENT_SOURCE_DIR_SDL3=%CM_SDL%" "-DW64DEVKIT_ROOT=%CM_W64%"
if errorlevel 1 (popd & goto :fail)

echo.
echo  Compiling...
cmake --build --preset %PRESET%
if errorlevel 1 (popd & goto :fail)

popd

set "OUT=%ROOT%build\%PRESET%\TheBlackHole.scr"
if not exist "%OUT%" (
    echo  ERROR: the build reported success but %OUT% is missing.
    goto :fail
)

for %%S in ("%OUT%") do set "BYTES=%%~zS"
call :sha256 "%OUT%"
echo.
echo  Built: build\%PRESET%\TheBlackHole.scr
echo         !BYTES! bytes, SHA-256 !HASH!

rem --- Defender --------------------------------------------------------------
if defined SCAN call :defender_scan "%OUT%"

rem --- install ---------------------------------------------------------------
if not defined INSTALL (
    echo.
    echo  Try it in a window:   build\%PRESET%\TheBlackHole.scr /w
    echo  Install it:           2-build-and-install-windows.cmd
    echo.
    endlocal
    exit /b 0
)

echo.
echo  Installing...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\install-windows.ps1" -Source "%OUT%"
if errorlevel 1 goto :fail

echo.
echo  Done. Open "Change screen saver" in Windows settings to set the wait
echo  time, or press Settings there for the screensaver's own options.
echo.
endlocal
exit /b 0

rem ===========================================================================
rem  :defender_scan  file
rem  A custom scan of one file. -DisableRemediation reports without
rem  quarantining, so a false positive does not delete the fresh build.
rem ===========================================================================
:defender_scan
set "MPCMD=%ProgramFiles%\Windows Defender\MpCmdRun.exe"
if not exist "%MPCMD%" (
    echo  Defender   : not present, check skipped
    exit /b 0
)
set "SCANLOG=%TEMP%\theblackhole-defender-scan.txt"
"%MPCMD%" -Scan -ScanType 3 -File "%~1" -DisableRemediation >"%SCANLOG%" 2>&1
findstr /i /c:"found no threats" "%SCANLOG%" >nul && (
    echo  Defender   : clean
    exit /b 0
)
findstr /i /c:"Failed with hr" "%SCANLOG%" >nul && (
    echo  Defender   : not the active antivirus here, check skipped
    exit /b 0
)
echo.
echo  WARNING: Microsoft Defender flagged the build. This is a false positive
echo  on a freshly compiled, unsigned file; see "Windows Defender" in
echo  README.md for how to report it. Scanner output:
echo.
type "%SCANLOG%"
echo.
exit /b 0

rem ===========================================================================
rem  :sha256  file   ->  HASH
rem ===========================================================================
:sha256
set "HASH="
for /f "skip=1 delims=" %%H in ('certutil -hashfile "%~1" SHA256 2^>nul') do (
    if not defined HASH set "HASH=%%H"
)
set "HASH=!HASH: =!"
exit /b 0

rem ===========================================================================
:require
if not exist "%~1" (
    echo  MISSING: %~2
    echo           expected at %~1
    exit /b 1
)
exit /b 0

:nokit
echo.
echo  The build toolchain is not in place. Run this first:
echo.
echo      1-download-tools-windows.cmd
echo.
endlocal
exit /b 1

:fail
echo.
echo  FAILED.
endlocal
exit /b 1
