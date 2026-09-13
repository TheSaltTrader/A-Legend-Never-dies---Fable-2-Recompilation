@echo off
setlocal
rem Launch fable2 against the extracted disc in game\.
rem
rem Log level info: a play session at debug wrote 25,000 debug lines an hour
rem (APC deliveries, guest input, user-context calls) for 12,000 of
rem everything else, and every line is formatted and flushed. Scripted runs
rem (tools/play_probe.py) keep debug; the crash record is critical either way.
rem
rem   tools\run.cmd [extra fable2 args...]
rem
rem Note: a bare `--flag` does NOT set a boolean cvar in this runtime - it is
rem accepted and silently ignored. Write `--fullscreen=true`.

set "PROJECT_ROOT=%~dp0.."
set "BUILD_TYPE=Release"
set "EXE=%PROJECT_ROOT%\out\build\win-amd64-%BUILD_TYPE%\fable2.exe"

if not exist "%EXE%" (
    echo ERROR: %EXE% not found - run tools\build.cmd first
    exit /b 1
)

"%EXE%" --game_data_root "%PROJECT_ROOT%\game" ^
        --log_file "%PROJECT_ROOT%\out\fable2.log" ^
        --log_level info ^
        %*
