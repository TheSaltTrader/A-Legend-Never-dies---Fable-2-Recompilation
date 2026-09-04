@echo off
setlocal
rem cmake --preset needs the SDK root as the working directory, and a
rem caller's CWD is not guaranteed (PowerShell's Set-Location does not
rem change what a child process inherits). Anchor to this script.
cd /d "%~dp0" || exit /b 1
rem Build the ReXGlue SDK with the Vulkan backend enabled.
rem
rem The shipped Windows plugin is D3D12 ONLY - rexgpu-xenos.dll contains zero
rem references to Vulkan - because REXGLUE_USE_VULKAN defaults OFF on Windows.
rem Fable II's scene renders flat blue through the D3D12 render-target path, so
rem a Vulkan build tests whether that path is the fault, without back-porting
rem 37 Canary GPU changes.
rem
rem Uses the project's OWN win-amd64 preset. Rolling the cmake line by hand
rem drops -march=x86-64-v2 and the SSSE3 intrinsics in core/memory.cpp then
rem fail to compile ("_mm_shuffle_epi8 requires target feature 'ssse3'").
rem No Vulkan SDK is required: Vulkan-Headers and glslang are vendored.
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "LLVM_BIN=C:\Program Files\LLVM\bin"
set "NINJA_BIN=%LOCALAPPDATA%\Programs\Python\Python312\Scripts"
call "%VCVARS%" >nul || exit /b 1
set "PATH=%LLVM_BIN%;%NINJA_BIN%;%PATH%"
rem REXGLUE_FIDELITYFX_SPATIAL_ONLY unlocks present_effect=cas/fsr. The FSR
rem and CAS shaders are already committed to this tree; only the TEMPORAL
rem fsr2/fsr3 path needs the ffx-api library, and it falls back on its own.
rem So this costs no fetch, no extra DLL and no shader compiler.
cmake --preset win-amd64 -DREXGLUE_USE_VULKAN=ON ^
      -DREXGLUE_FIDELITYFX_SPATIAL_ONLY=ON || exit /b 1
cmake --build out/build/win-amd64 --config Release || exit /b 1
echo === Done ===
