@echo off
rem ===========================================================================
rem  The Black Hole - install the portable build toolchain (Windows x64)
rem
rem  Downloads and unpacks everything needed to build the screensaver into
rem  C:\workenv (override with the first argument or the BH_WORKENV variable).
rem  Nothing is installed system-wide, no registry keys, no PATH changes:
rem  every tool is unzip-and-run and lives under that one folder.
rem
rem  All four archives come from the projects' official release pages and are
rem  verified against pinned SHA-256 hashes before they are unpacked.
rem
rem  Usage:  1-download-tools-windows.cmd [workenv-dir] [force]
rem            force   re-unpack even if the tool folder already exists
rem ===========================================================================
setlocal enabledelayedexpansion

set "WORKENV=C:\workenv"
if not "%BH_WORKENV%"=="" set "WORKENV=%BH_WORKENV%"
set "FORCE="

for %%A in (%*) do (
    if /i "%%~A"=="force" (set "FORCE=1") else (set "WORKENV=%%~A")
)

set "DOWNLOADS=%WORKENV%\downloads"

rem --- pinned versions ------------------------------------------------------
set "V_W64DEVKIT=2.9.1"
set "V_CMAKE=4.4.3"
set "V_NINJA=1.13.2"
set "V_SDL=3.4.16"

set "D_W64DEVKIT=%WORKENV%\w64devkit"
set "D_CMAKE=%WORKENV%\cmake-%V_CMAKE%-windows-x86_64"
set "D_NINJA=%WORKENV%\ninja"
set "D_SDL=%WORKENV%\SDL3-%V_SDL%"

echo.
echo  The Black Hole - Windows build toolchain
echo  ----------------------------------------
echo  target folder : %WORKENV%
echo.

rem --- the two things Windows itself has to provide -------------------------
where curl.exe >nul 2>&1 || (echo ERROR: curl.exe not found ^(needs Windows 10 1803 or newer^).& goto :fail)

rem  Windows' own bsdtar, called by full path on purpose: it unpacks both .zip
rem  and .tar.gz, while a GNU tar from Git or MSYS may come first on PATH and
rem  cannot read zip archives at all.
set "TAR=%SystemRoot%\System32\tar.exe"
if not exist "%TAR%" (echo ERROR: %TAR% not found ^(needs Windows 10 1803 or newer^).& goto :fail)

rem --- 7-Zip is used to unpack w64devkit without running the downloaded exe --
set "SEVENZIP="
for %%P in ("%ProgramFiles%\7-Zip\7z.exe" "%ProgramFiles(x86)%\7-Zip\7z.exe") do if exist %%P set "SEVENZIP=%%~P"
if not defined SEVENZIP for /f "delims=" %%P in ('where 7z.exe 2^>nul') do if not defined SEVENZIP set "SEVENZIP=%%P"

if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%" || goto :fail

echo  [1/4] w64devkit %V_W64DEVKIT%  (GCC 16.2.0, binutils, windres, make, gdb)
call :fetch "w64devkit-x64-%V_W64DEVKIT%.7z.exe" ^
    "https://github.com/skeeto/w64devkit/releases/download/v%V_W64DEVKIT%/w64devkit-x64-%V_W64DEVKIT%.7z.exe" ^
    "9208c19755cd4964b7915b9afcf02c66d493a4c870c4b3e83f6c538d9c1237a5" || goto :fail

echo  [2/4] CMake %V_CMAKE%
call :fetch "cmake-%V_CMAKE%-windows-x86_64.zip" ^
    "https://github.com/Kitware/CMake/releases/download/v%V_CMAKE%/cmake-%V_CMAKE%-windows-x86_64.zip" ^
    "4d52ebab7193a698651639ed80d8d04fd903358843572cf44c7fd234cb7c26ab" || goto :fail

echo  [3/4] Ninja %V_NINJA%
call :fetch "ninja-win-%V_NINJA%.zip" ^
    "https://github.com/ninja-build/ninja/releases/download/v%V_NINJA%/ninja-win.zip" ^
    "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65" || goto :fail

echo  [4/4] SDL3 %V_SDL% (source; built statically into the screensaver)
call :fetch "SDL3-%V_SDL%.tar.gz" ^
    "https://github.com/libsdl-org/SDL/releases/download/release-%V_SDL%/SDL3-%V_SDL%.tar.gz" ^
    "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68" || goto :fail

echo.
echo  Unpacking...

rem --- w64devkit: a 7-Zip self-extracting archive ---------------------------
call :unpacked "%D_W64DEVKIT%" && goto :skip_w64
if defined SEVENZIP (
    "%SEVENZIP%" x -y -o"%WORKENV%" "%DOWNLOADS%\w64devkit-x64-%V_W64DEVKIT%.7z.exe" >nul || goto :fail
) else (
    echo   [note] 7-Zip not found - letting the archive extract itself
    "%DOWNLOADS%\w64devkit-x64-%V_W64DEVKIT%.7z.exe" -y -o"%WORKENV%" || goto :fail
)
echo   [ok]   w64devkit
:skip_w64

rem --- the rest: plain zip / tar.gz, handled by the built-in bsdtar ---------
call :unpacked "%D_CMAKE%" && goto :skip_cmake
"%TAR%" -xf "%DOWNLOADS%\cmake-%V_CMAKE%-windows-x86_64.zip" -C "%WORKENV%" || goto :fail
echo   [ok]   cmake
:skip_cmake

call :unpacked "%D_NINJA%" && goto :skip_ninja
if not exist "%D_NINJA%" mkdir "%D_NINJA%"
"%TAR%" -xf "%DOWNLOADS%\ninja-win-%V_NINJA%.zip" -C "%D_NINJA%" || goto :fail
echo   [ok]   ninja
:skip_ninja

call :unpacked "%D_SDL%" && goto :skip_sdl
"%TAR%" -xzf "%DOWNLOADS%\SDL3-%V_SDL%.tar.gz" -C "%WORKENV%" || goto :fail
echo   [ok]   SDL3
:skip_sdl

rem --- prove the toolchain actually runs ------------------------------------
echo.
echo  Verifying...
set "PATH=%D_CMAKE%\bin;%D_NINJA%;%D_W64DEVKIT%\bin;%PATH%"
for /f "delims=" %%V in ('gcc --version 2^>nul')     do if not defined S_GCC   set "S_GCC=%%V"
for /f "delims=" %%V in ('cmake --version 2^>nul')   do if not defined S_CMAKE set "S_CMAKE=%%V"
for /f "delims=" %%V in ('ninja --version 2^>nul')   do if not defined S_NINJA set "S_NINJA=%%V"
for /f "delims=" %%V in ('windres --version 2^>nul') do if not defined S_RC    set "S_RC=%%V"

if not defined S_GCC   (echo ERROR: gcc did not run.&     goto :fail)
if not defined S_CMAKE (echo ERROR: cmake did not run.&   goto :fail)
if not defined S_NINJA (echo ERROR: ninja did not run.&   goto :fail)
if not exist "%D_SDL%\CMakeLists.txt" (echo ERROR: SDL3 source is incomplete.& goto :fail)

echo   !S_GCC!
echo   !S_RC!
echo   !S_CMAKE!
echo   ninja !S_NINJA!
echo   SDL3 %V_SDL% source at %D_SDL%
echo.
echo  Done. The archives are kept in %DOWNLOADS% so the environment can be
echo  rebuilt on another machine without downloading again.
echo.
echo  Next: 2-build-and-install-windows.cmd
if /i not "%WORKENV%"=="C:\workenv" (
    echo.
    echo  The tools are not in the default place, so tell the build where:
    echo      set BH_WORKENV=%WORKENV%
)
echo.
endlocal
exit /b 0

rem ===========================================================================
rem  :fetch  filename  url  sha256
rem  Downloads unless a byte-identical copy is already in the downloads folder.
rem ===========================================================================
:fetch
set "F=%DOWNLOADS%\%~1"
if exist "%F%" (
    call :sha256 "%F%"
    if /i "!HASH!"=="%~3" (
        echo   [ok]   %~1 already downloaded
        exit /b 0
    )
    echo   [warn] %~1 does not match its pinned hash - downloading again
    del /f /q "%F%" >nul 2>&1
)
echo   [get]  %~1
curl -L --fail --progress-bar -o "%F%" "%~2"
if errorlevel 1 (
    echo   ERROR: download failed: %~2
    exit /b 1
)
call :sha256 "%F%"
if /i not "!HASH!"=="%~3" (
    echo   ERROR: SHA-256 mismatch for %~1 - the file was NOT what we expected.
    echo          expected %~3
    echo          actual   !HASH!
    del /f /q "%F%" >nul 2>&1
    exit /b 1
)
echo   [ok]   %~1 verified
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
rem  :unpacked  dir   ->  succeeds if the folder is already there and we are
rem                       not being asked to redo it
rem ===========================================================================
:unpacked
if defined FORCE exit /b 1
if exist "%~1" (
    echo   [ok]   %~nx1 already unpacked
    exit /b 0
)
exit /b 1

:fail
echo.
echo  FAILED.
endlocal
exit /b 1
