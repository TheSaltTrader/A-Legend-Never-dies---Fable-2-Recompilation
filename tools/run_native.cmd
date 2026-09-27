@echo off
setlocal
rem Play with the NATIVE window beside the plugin, in the best configuration measured (phase A, updated 2026-09-26):
rem   - the bridge replay draws every pair through the SDK's own DXBC translator (ngpu_sdk_path + "*:*"),
rem     including point/rect/quad lists (the plugin's generated GS) and the tessellated terrain (its HS/DS);
rem   - load-time frames are merged, not dropped (ngpu_bridge_accumulate) - the beach's shore mask needs them;
rem   - the window shows what the CONSOLE swaps (ngpu_present_post): front end, world and HUD;
rem   - the replay runs on its own thread (ngpu_async_replay, build default since 2026-09-26): the game keeps its own
rem     pace (60 fps at Bower Lake, was ~14) and the native window shows the latest frame the replay finished.
rem The defaults of the build are unchanged; this only sets FABLE2_TUNE for this launch.
rem
rem   tools\run_native.cmd [--inline-plugin | --backend] [extra fable2 args...]
rem
rem --inline-plugin (2026-09-26): run with the locally built plugin that also hands off INLINE shader microcode
rem (rexglue-src f495e67, never pushed; D:\fable2_flash\sdk_legs\bin_main_head_imm). Without it the native window runs
rem the previous shader for every inline-loaded one: the lake's gold rectangle, no post effects. With it the lake,
rem post chain, far trees and water/wind animation tables are fixed (docs/native_gpu/HANDOVER.md, 2026-09-25 night).
rem The two DLLs in the build folder are backed up first and put back when the game exits.
rem
rem Known open rows (docs/native_gpu/HANDOVER.md): loading-screen background (u5), the right-shore hill wedge,
rem u2 halo, EDRAM ownership transfer.

set "HERE=%~dp0"
set "BIN=%HERE%..\out\build\win-amd64-Release"
set "INLINE=D:\fable2_flash\sdk_legs\bin_main_head_imm"
set "BACKUP=%BIN%\dll_before_inline_plugin"
set "USE_INLINE="
set "USE_BACKEND="
rem The flag is removed from the argument text itself: shift would move %0 as well, and %1..%9 split "a=b" at the "=".
set "ARGS=%*"
if /i "%~1"=="--inline-plugin" (set "USE_INLINE=1" & set "ARGS=%ARGS:--inline-plugin=%")
rem --backend (2026-09-26): the BACKEND TRANSPLANT - the plugin's own D3D12 backend running in-app as the ONLY GPU
rem (gpu_offload_to_native: the plugin keeps its PM4 parser + the bridge; its own window stays BLACK, that is expected).
rem Uses the plugin pair in D:\fable2_flash\sdk_legs\bin_offload (rexglue-src 17309f4, never pushed), staged like
rem --inline-plugin. Measured: lake 60 fps, town 33 fps, picture matching the plugin (HANDOVER 2026-09-26 midday).
rem Updated 2026-09-26 night: the final pair D:\fable2_flash\sdk_legs\bin_native_final2 (rexglue-src febe384: dirty
rem bitmap, type-0 range path, mark-on-change, measured-dead counters, plugin reveal hold, texture-pack counter fix).
if /i "%~1"=="--backend" (set "USE_INLINE=1" & set "USE_BACKEND=1" & set "INLINE=D:\fable2_flash\sdk_legs\bin_native_final2" & set "ARGS=%ARGS:--backend=%")
rem --dll (2026-09-26): the same backend through ngpu_backend.dll (the game-agnostic module, its own window) - the
rem executable's native path stays off. Same pair as --backend.
set "USE_DLL="
if /i "%~1"=="--dll" (set "USE_INLINE=1" & set "USE_DLL=1" & set "INLINE=D:\fable2_flash\sdk_legs\bin_native_final2" & set "ARGS=%ARGS:--dll=%")

echo EXPERIMENTAL native configuration (phase A) - NOT the default build: native window + bridge replay + SDK DXBC path + present-what-the-console-swaps.
set "FABLE2_TUNE=ngpu_shadow=true;ngpu_native_draws=true;ngpu_bridge=true;ngpu_bridge_log=true;ngpu_bridge_draws=true;ngpu_hooked_draws=false;ngpu_sdk_path=true;ngpu_sdk_pairs=*:*;ngpu_sdk_world_only=false;ngpu_bridge_accumulate=true;ngpu_present_post=true;ngpu_post_raw=true"
if defined USE_BACKEND (
    set "FABLE2_TUNE=%FABLE2_TUNE%;ngpu_backend=true;gpu_offload_to_native=true"
    echo BACKEND TRANSPLANT: the native window is the only renderer; the plugin's window stays black.
)
if defined USE_DLL (
    set "FABLE2_TUNE=ngpu_backend_dll=true;gpu_offload_to_native=true;ngpu_bridge=false;ngpu_shadow=false;ngpu_backend=false"
    echo NGPU_BACKEND.DLL: the game-agnostic backend renders in its own window; the plugin's window stays black.
)

if not defined USE_INLINE goto launch
if not exist "%INLINE%\rexgpu-xenos.dll" (echo ERROR: %INLINE%\rexgpu-xenos.dll not found & exit /b 1)
if not exist "%BACKUP%" mkdir "%BACKUP%"
copy /y "%BIN%\rexgpu-xenos.dll" "%BACKUP%\" >nul || exit /b 1
copy /y "%BIN%\rexruntime.dll" "%BACKUP%\" >nul || exit /b 1
copy /y "%INLINE%\rexgpu-xenos.dll" "%BIN%\" >nul || exit /b 1
copy /y "%INLINE%\rexruntime.dll" "%BIN%\" >nul || exit /b 1
rem A marker while the build folder holds a STAGED pair: if this script dies before the restore (Ctrl+C, a closed
rem terminal, a reboot), the folder must not silently keep a test plugin - the marker says so and names the backup.
> "%BIN%\STAGED.txt" echo STAGED plugin pair from %INLINE% by run_native.cmd - the original DLLs are in %BACKUP%; copy them back if this file is still here after the game has exited.
echo Using the inline-microcode plugin; the previous DLLs are in %BACKUP% and are restored on exit.

:launch
call "%HERE%run.cmd" %ARGS%
set "RC=%ERRORLEVEL%"
if defined USE_INLINE (
    copy /y "%BACKUP%\rexgpu-xenos.dll" "%BIN%\" >nul
    copy /y "%BACKUP%\rexruntime.dll" "%BIN%\" >nul
    del "%BIN%\STAGED.txt" 2>nul
    echo Restored the previous plugin DLLs.
)
exit /b %RC%
