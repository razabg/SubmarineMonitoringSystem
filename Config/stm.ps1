# stm.ps1 - switch the Nucleo board between Windows (flashing) and WSL (UART testing)
#
# Usage (after the "stm" function is set up in your PowerShell profile):
#   stm win     -> give the board to Windows, so CubeIDE can flash/debug
#   stm linux   -> give the board to WSL, starts an auto-attach watcher so
#                  unplug/replug recovers on its own, no need to run this
#                  again after a physical disconnect
#   stm         -> show current usbipd state
#
# Change $busid to match your machine - find it with: usbipd list

param([string]$action)
$busid = "1-13"

function Stop-AutoAttach {
    Get-CimInstance Win32_Process -Filter "Name = 'usbipd.exe'" |
        Where-Object { $_.CommandLine -match "auto-attach" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

if ($action -eq "win") {
    Stop-AutoAttach
    Start-Sleep -Milliseconds 500

    # The auto-attach watcher can reattach the device one more time right
    # after being killed - its loop hasn't fully torn down yet. The first
    # detach call gets undone by that; a second detach makes it stick.
    usbipd detach --busid $busid
    Start-Sleep -Milliseconds 500
    usbipd detach --busid $busid

    Write-Host "-> Windows (CubeIDE can flash)"
}
elseif ($action -eq "linux") {
    Stop-AutoAttach
    Start-Process usbipd -ArgumentList "attach --wsl --busid $busid --auto-attach" -WindowStyle Minimized
    Write-Host "-> WSL, watching for unplug/replug (minimized usbipd window)"
}
else {
    usbipd list
}