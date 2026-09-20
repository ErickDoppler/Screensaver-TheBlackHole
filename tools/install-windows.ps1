# Installs the built screensaver for the current user (no admin needed) and
# makes it the active screensaver. Run from a normal PowerShell.
#
# The full path of a permanent copy is what goes into the registry. Explorer's
# right-click "Install" instead pins the .scr where it happens to sit, so a
# copy in the build folder or in Downloads stops working once it is gone, and
# Windows then does nothing at all on idle, without a word.
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

$key     = 'HKCU:\Control Panel\Desktop'
$dest    = Join-Path $env:LOCALAPPDATA 'TheBlackHole'
$target  = Join-Path $dest 'TheBlackHole.scr'

# Tells Windows the screensaver settings changed, so they apply now rather than
# at the next sign-in.
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
    $current = (Get-ItemProperty $key -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
    if ($current -and ($current -ieq $target)) {
        Remove-ItemProperty $key -Name 'SCRNSAVE.EXE'
        Set-ItemProperty $key -Name 'ScreenSaveActive' -Value '0'
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

$previous = (Get-ItemProperty $key -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'

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

Set-ItemProperty $key -Name 'SCRNSAVE.EXE' -Value $target
Set-ItemProperty $key -Name 'ScreenSaveActive' -Value '1'
# Without a timeout Windows never starts a screensaver; 10 minutes if unset.
$timeout = (Get-ItemProperty $key -Name 'ScreenSaveTimeOut' -ErrorAction SilentlyContinue).ScreenSaveTimeOut
if (-not $timeout -or [int]$timeout -le 0) {
    $timeout = '600'
    Set-ItemProperty $key -Name 'ScreenSaveTimeOut' -Value $timeout
}
Update-ScreenSaverActive $true

$version = (Get-Item $target).VersionInfo.FileVersion
$hash = (Get-FileHash $target -Algorithm SHA256).Hash.ToLower()
Write-Host "Installed $target (version $version) and selected it as the active screensaver."
Write-Host "SHA-256   $hash"

if ($previous -and $previous -ne $target -and -not (Test-Path $previous)) {
    Write-Host ''
    Write-Host "Note: Windows had been pointed at $previous, which does not exist -"
    Write-Host '      that is why nothing happened on idle. It is fixed now.'
    # The usual way that happens: a 32-bit screensaver dialog (or a copy made
    # by a 32-bit tool) writes "system32" while actually landing in SysWOW64.
    # Winlogon is 64-bit, looks in the real system32, finds nothing, and starts
    # nothing - while the dialog's own preview still works.
    $stale = Join-Path $env:WINDIR 'SysWOW64\TheBlackHole.scr'
    if ($previous -match '(?i)system32' -and (Test-Path $stale)) {
        Write-Host ''
        Write-Host "      The old copy is in $stale - the 32-bit system folder."
        Write-Host '      Windows looks for screensavers in the 64-bit one, so that copy can'
        Write-Host '      never run. Delete it from an administrator prompt if you want it gone:'
        Write-Host "          del `"$stale`""
    }
}

Write-Host ''
Write-Host "It starts after $timeout seconds of no input."
Write-Host 'Open "Change screen saver" in Windows settings to adjust the timeout, or'
Write-Host 'press Settings there for the screensaver''s own options.'
Write-Host 'The dropdown there only lists screensavers in C:\Windows\System32, so'
Write-Host 'this one is not in it; picking another entry replaces this setting.'
