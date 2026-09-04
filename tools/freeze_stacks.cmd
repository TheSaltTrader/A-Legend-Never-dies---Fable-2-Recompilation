@echo off
setlocal
rem Dump every thread's stack while the game is HUNG, not crashed.
rem
rem   tools\freeze_stacks.cmd [seconds-before-breaking]
rem
rem The freeze is not a fault: the guest keeps running its main loop (the
rem presence heartbeat still ticks once a second) and simply stops submitting
rem GPU work. So there is no exception for the debugger to catch, and
rem all_threads-style "-g, first prompt is the crash" does not apply.
rem
rem Instead: run under cdb, let it get well past the freeze, then break in on a
rem timer and dump all stacks. -c runs the command list once the target is
rem loaded; the .sleep gives the game time to reach the frozen state before
rem ~*k prints where every thread actually is.
rem
rem RelWithDebInfo, not Release: codegen emits one source line per guest
rem instruction, so a guest frame resolves to the exact PowerPC instruction.

set "PROJECT_ROOT=%~dp0.."
set "BUILD_DIR=%PROJECT_ROOT%\out\build\win-amd64-RelWithDebInfo"
set "CDB=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
set "_NT_SYMBOL_PATH=%BUILD_DIR%"

set "WAIT_MS=%~1"
if "%WAIT_MS%"=="" set "WAIT_MS=260000"

if not exist "%BUILD_DIR%\fable2.exe" (
    echo ERROR: %BUILD_DIR%\fable2.exe not found
    echo        Build it first:  tools\build.cmd RelWithDebInfo
    exit /b 1
)

echo Running for %WAIT_MS% ms, then breaking in and dumping all stacks...
"%CDB%" -o -c ".sleep %WAIT_MS%; ~*k 30; !locks; q" "%BUILD_DIR%\fable2.exe" ^
    --game_data_root "%PROJECT_ROOT%\game" ^
    --log_file "%PROJECT_ROOT%\out\freeze.log" --log_level info ^
    > "%PROJECT_ROOT%\out\freeze_stacks.txt" 2>&1

echo Wrote %PROJECT_ROOT%\out\freeze_stacks.txt
