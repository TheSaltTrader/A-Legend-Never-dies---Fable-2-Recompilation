@echo off
setlocal
rem Launch fable2 against the extracted disc in game\.
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
        --log_level debug ^
        %*
