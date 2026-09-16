@echo off
rem Puts the portable open-source toolchain from C:\workenv on PATH for this shell.
set W64DEVKIT=C:\workenv\w64devkit
set PATH=%W64DEVKIT%\bin;C:\workenv\cmake-4.4.3-windows-x86_64\bin;C:\workenv\ninja;%PATH%
echo TheBlackHole dev environment: gcc, make, gdb, windres, cmake, ninja are on PATH.
