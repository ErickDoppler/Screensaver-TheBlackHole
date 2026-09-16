# Installs the built screensaver for the current user (no admin needed) and
# makes it the active screensaver, or removes it again with -Uninstall.
#
#   powershell -ExecutionPolicy Bypass -File tools\install-windows.ps1
#   powershell -ExecutionPolicy Bypass -File tools\install-windows.ps1 -Uninstall
#
# -Source   the .scr to install (default: build\win-mingw\TheBlackHole.scr)
# -Purge    with -Uninstall, also forget the saved settings
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\build\win-mingw\TheBlackHole.scr'),
    [switch]$Uninstall,
    [switch]$Purge
)
$ErrorActionPreference = 'Stop'

$dest    = Join-Path $env:LOCALAPPDATA 'TheBlackHole'
$target  = Join-Path $dest 'TheBlackHole.scr'
$desktop = 'HKCU:\Control Panel\Desktop'

# Tells Windows the screensaver setting changed, so it applies now rather
# than at the next sign-in.
Add-Type -Namespace BlackHole -Name Native -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true)]
public static extern bool SystemParametersInfo(uint action, uint param, System.IntPtr vparam, uint flags);
'@
function Update-ScreenSaverActive([bool]$on) {
    $SPI_SETSCREENSAVEACTIVE = 0x0011
    $flags = 0x01 -bor 0x02   # SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
    [void][BlackHole.Native]::SystemParametersInfo($SPI_SETSCREENSAVEACTIVE, [uint32]$on, [IntPtr]::Zero, $flags)
}

if ($Uninstall) {
    $current = (Get-ItemProperty $desktop -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
    if ($current -and ($current -ieq $target)) {
        Remove-ItemProperty $desktop -Name 'SCRNSAVE.EXE'
        Set-ItemProperty $desktop -Name 'ScreenSaveActive' -Value '0'
        Update-ScreenSaverActive $false
        Write-Host 'The Black Hole is no longer the active screensaver.'
    }
    if (Test-Path $dest) {
        Remove-Item $dest -Recurse -Force
        Write-Host "Removed $dest"
    }
    if ($Purge -and (Test-Path 'HKCU:\Software\TheBlackHole')) {
        Remove-Item 'HKCU:\Software\TheBlackHole' -Recurse -Force
        Write-Host 'Removed the saved settings.'
    }
    exit 0
}

if (-not (Test-Path $Source)) {
    Write-Error "Nothing to install at $Source - build first: 2-build-and-install-windows.cmd"
    exit 1
}

New-Item -ItemType Directory -Force $dest | Out-Null
try {
    Copy-Item $Source $target -Force
} catch {
    Write-Error ("Could not copy the screensaver. If it is running (or its settings are open), " +
                 "close it and try again. If Defender removed it, see 'Windows Defender' in README.md.`n$_")
    exit 1
}
# A locally built file has no Mark of the Web, but a copied-in download would,
# and SmartScreen then blocks the screensaver from starting.
Unblock-File $target

Set-ItemProperty $desktop -Name 'SCRNSAVE.EXE' -Value $target
Set-ItemProperty $desktop -Name 'ScreenSaveActive' -Value '1'
Update-ScreenSaverActive $true

$hash = (Get-FileHash $target -Algorithm SHA256).Hash
Write-Host "Installed $target"
Write-Host "SHA-256   $hash"
Write-Host 'It is now the active screensaver for this user.'
