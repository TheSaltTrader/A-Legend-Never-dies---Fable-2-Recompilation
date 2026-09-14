# End-to-end test of the in-game updater, with nobody at the mouse.
#
# The fable2.exe in -InstallDir starts with FABLE2_UPDATE_AUTO=1 (the panel
# presses Update now and Restart now itself) and, when -Pretend is given,
# FABLE2_UPDATE_PRETEND_VERSION so it believes it is that old. It finds the
# newest GitHub release, downloads the zip, installs it over -InstallDir,
# restarts, and the restarted process cleans up and quits after -QuitAfter
# seconds (Escape's path). The script watches the log and then reports the
# executable's hash against -ExpectExe, any .old left, and the update folder.
#
#   tools\update_e2e_test.ps1                              # build dir, pretend 0.1.0
#   tools\update_e2e_test.ps1 -InstallDir D:\scratch\v0.2.0 -Pretend "" -ExpectExe ..\Releases\v0.2.1\fable2.exe
#
# Launches through WMI so the game is not a child of this shell. Refuses to
# start while any fable2.exe is running - a window opening on someone's
# screen reads as their game. See docs/UPDATER.md.
param(
  [string]$InstallDir = "",
  [string]$Pretend = "0.1.0",
  [string]$ExpectExe = "",
  [int]$QuitAfter = 150
)
$root = Split-Path -Parent $PSScriptRoot
if (-not $InstallDir) { $InstallDir = "$root\out\build\win-amd64-Release" }
if (-not $ExpectExe) { $ExpectExe = "$root\..\Releases\v" + (Get-Content "$root\VERSION").Trim() + "\fable2.exe" }
if (Get-Process fable2 -ErrorAction SilentlyContinue) { "a fable2.exe is already running - not starting"; exit 1 }

$before = (Get-FileHash "$InstallDir\fable2.exe").Hash.Substring(0, 16)
$expect = (Get-FileHash $ExpectExe).Hash.Substring(0, 16)
"install dir: $InstallDir"
"exe before: $before   expected after: $expect   (must differ for the install to be visible)"
$log1 = "$root\out\update_test_parent.log"
Remove-Item $log1 -ErrorAction SilentlyContinue
$logsBefore = @(Get-ChildItem "$InstallDir\logs\fable2_*.log" -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })

$pretendSet = if ($Pretend) { "set FABLE2_UPDATE_PRETEND_VERSION=$Pretend&& " } else { "" }
$cmd = "cmd.exe /c `"set FABLE2_UPDATE_AUTO=1&& ${pretendSet}set FABLE2_QUIT_AFTER=$QuitAfter&& start `"`" `"$InstallDir\fable2.exe`" --game_data_root `"$root\game`" --log_file `"$log1`" --log_level info`""
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = $InstallDir }
$launched = Get-Date
"launched: ReturnValue $($r.ReturnValue) at $($launched.ToString('HH:mm:ss'))"

$deadline = $launched.AddSeconds($QuitAfter * 2 + 90)
$seen = 0
$quiet = 0
while ((Get-Date) -lt $deadline) {
  Start-Sleep 5
  if (Test-Path $log1) {
    $lines = @(Select-String -Path $log1 -Pattern "\[update\]|Escape: quitting|Setup screen|Quit seam" | ForEach-Object { $_.Line })
    if ($lines.Count -gt $seen) {
      $lines[$seen..($lines.Count - 1)] | ForEach-Object { "  parent: " + $_.Substring(0, [Math]::Min(190, $_.Length)) }
      $seen = $lines.Count
    }
  }
  $procs = @(Get-Process fable2 -ErrorAction SilentlyContinue)
  if ($procs.Count -eq 0 -and ((Get-Date) -gt $launched.AddSeconds(20))) { $quiet++ } else { $quiet = 0 }
  if ($quiet -ge 2) { break }
}
"--- after ($(Get-Date -Format HH:mm:ss)) ---"
$after = (Get-FileHash "$InstallDir\fable2.exe").Hash.Substring(0, 16)
"exe after:  $after   " + $(if ($after -eq $expect) { "== expected: INSTALLED" } else { "!= expected: NOT installed" })
"old files left: " + @(Get-ChildItem "$InstallDir" -Recurse -Filter "*.old" -ErrorAction SilentlyContinue).Count
"update folder: " + $(if (Test-Path "$InstallDir\update") { (Get-ChildItem "$InstallDir\update" -Recurse | ForEach-Object { $_.Name }) -join ", " } else { "gone (cleaned)" })
"tools beside the exe: " + @(Get-ChildItem "$InstallDir\tools" -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
$newLogs = @(Get-ChildItem "$InstallDir\logs\fable2_*.log" -ErrorAction SilentlyContinue | Where-Object { $logsBefore -notcontains $_.Name } | Sort-Object LastWriteTime)
foreach ($l in $newLogs) {
  "--- child log $($l.Name) (no --log_file: the relaunch dropped the arguments) ---"
  Select-String -Path $l.FullName -Pattern "\[update\]|Escape: quitting|Setup screen|previous instance|Quit seam" | ForEach-Object { "  child: " + $_.Line.Substring(0, [Math]::Min(190, $_.Line.Length)) }
}
if (Test-Path $log1) {
  "--- parent log, lines after the hand-over (the child writes here when it kept the arguments) ---"
  Select-String -Path $log1 -Pattern "previous instance|cleaned up|up to date|Setup screen|Quit seam|Escape: quitting" | Select-Object -Last 8 | ForEach-Object { "  " + $_.Line.Substring(0, [Math]::Min(190, $_.Line.Length)) }
}
"processes left: " + @(Get-Process fable2 -ErrorAction SilentlyContinue).Count
