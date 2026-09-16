# Puts the portable open-source toolchain from C:\workenv on PATH for this PowerShell session.
$env:W64DEVKIT = 'C:\workenv\w64devkit'
$env:PATH = "$env:W64DEVKIT\bin;C:\workenv\cmake-4.4.3-windows-x86_64\bin;C:\workenv\ninja;$env:PATH"
Write-Host 'TheBlackHole dev environment: gcc, make, gdb, windres, cmake, ninja are on PATH.'
