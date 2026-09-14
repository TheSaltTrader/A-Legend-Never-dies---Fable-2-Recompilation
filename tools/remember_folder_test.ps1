# Two short launches of the build-dir fable2.exe:
#   1) with --game_data_root: the folder must be written into the settings
#      ("Game folder from the command line remembered" in the log);
#   2) with no arguments at all: no setup screen, the guest boots.
# Each run quits by Escape's path after -QuitAfter seconds (FABLE2_QUIT_AFTER).
# Launches through WMI; refuses to start while any fable2.exe is running.
# See docs/UPDATER.md.
param([int]$QuitAfter = 25)
$root = Split-Path -Parent $PSScriptRoot
$bd = "$root\out\build\win-amd64-Release"
if (Get-Process fable2 -ErrorAction SilentlyContinue) { "a fable2.exe is already running - not starting"; exit 1 }
$cfg = "$bd\fable2_settings.cfg"
"before: " + ((Select-String -Path $cfg -Pattern "^configured=|^game_path=" | ForEach-Object { $_.Line }) -join "  ")

# Not $args: that is PowerShell's own automatic variable, and a parameter of
# that name arrives empty - which once ran both launches without arguments.
function Launch($launchArgs) {
  $cmd = "cmd.exe /c `"set FABLE2_QUIT_AFTER=$QuitAfter&& start `"`" `"$bd\fable2.exe`" $launchArgs`""
  $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = $bd }
  "launched ($launchArgs): rc $($r.ReturnValue) at $(Get-Date -Format HH:mm:ss)"
  $t0 = Get-Date
  Start-Sleep 8
  while ((Get-Process fable2 -ErrorAction SilentlyContinue) -and ((Get-Date) -lt $t0.AddSeconds($QuitAfter + 40))) { Start-Sleep 3 }
  "ended at $(Get-Date -Format HH:mm:ss); processes left: " + @(Get-Process fable2 -ErrorAction SilentlyContinue).Count
}

$log1 = "$root\out\remember_test_1.log"
Remove-Item $log1 -ErrorAction SilentlyContinue
Launch "--game_data_root `"$root\game`" --log_file `"$log1`" --log_level info"
"run 1 log: " + ((Select-String -Path $log1 -Pattern "remembered|Setup screen|\[update\] up to date|Escape: quitting" | ForEach-Object { $_.Line.Substring(24, [Math]::Min(150, $_.Line.Length - 24)) }) -join " | ")
"after run 1: " + ((Select-String -Path $cfg -Pattern "^configured=|^game_path=" | ForEach-Object { $_.Line }) -join "  ")

$logsBefore = @(Get-ChildItem "$bd\logs\fable2_*.log" -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
Launch ""
$newLog = Get-ChildItem "$bd\logs\fable2_*.log" -ErrorAction SilentlyContinue | Where-Object { $logsBefore -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
if ($newLog) {
  "run 2 log ($($newLog.Name)): " + ((Select-String -Path $newLog.FullName -Pattern "remembered|Setup screen|\[update\] up to date|Escape: quitting|launching guest module" | ForEach-Object { $_.Line.Substring(24, [Math]::Min(150, $_.Line.Length - 24)) }) -join " | ")
} else { "run 2: no new log found" }
