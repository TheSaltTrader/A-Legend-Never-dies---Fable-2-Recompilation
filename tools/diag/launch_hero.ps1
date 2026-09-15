# Launch fable2.exe from the build dir with a pad script (hero 1 = LEFT slot =
# forest by default) and optional FABLE2_TUNE overrides. Refuses if a fable2.exe
# already runs (the user's session). Prints the PID it started.
param(
  [string]$Pad = "12:a,16:down,18:a,22:a",
  [string]$Tune = "",
  [string]$Log = ""
)
if (Get-Process fable2 -ErrorAction SilentlyContinue) { "a fable2.exe is already running - not starting another"; exit 1 }
$root = "C:\users\renoi\claudecode\Fable 2 Recompile Xbox\fable2recomp"
$exe = "$root\out\build\win-amd64-Release\fable2.exe"
if (-not $Log) { $Log = "$root\out\fable2.log" }
Remove-Item $Log -ErrorAction SilentlyContinue
$env:FABLE2_PAD_SCRIPT = $Pad
if ($Tune) { $env:FABLE2_TUNE = $Tune } else { Remove-Item Env:FABLE2_TUNE -ErrorAction SilentlyContinue }
$env:FABLE2_HUD = "1"
$before = @(Get-Process fable2 -ErrorAction SilentlyContinue | ForEach-Object Id)
# Start-Process, not WMI: a WMI-created process does not inherit this shell's
# environment, so FABLE2_TUNE / FABLE2_PAD_SCRIPT were silently dropped.
$r = Start-Process -FilePath $exe -ArgumentList @("--game_data_root", "`"$root\game`"", "--log_file", "`"$Log`"", "--log_level", "info", "--log_flush_interval", "1") -WorkingDirectory "$root\out\build\win-amd64-Release" -PassThru
Start-Sleep -Seconds 3
$p = Get-Process fable2 -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Id } | Select-Object -First 1
if ($p) { "launched pid $($p.Id)  log $Log" } else { "launch returned $($r.ReturnValue) but no fable2.exe seen yet" }
