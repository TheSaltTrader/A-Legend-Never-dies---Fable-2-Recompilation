@echo off
rem P2 census build: inject the exit hooks into the generated code, then COMPILE ONLY. tools\build.cmd starts with
rem the codegen, which rewrites the generated files and silently drops the injected exits (2026-09-27: a census exe
rem with 0 exits and 242 M stack overflows). Run tools\build.cmd once first (it configures and generates); run
rem tools\native_gpu\p2_inject_exits.py --revert and tools\build.cmd again before any release build.
setlocal
set "PROJECT_ROOT=%~dp0.."
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "LLVM_BIN=C:\Program Files\LLVM\bin"
set "NINJA_BIN=%LOCALAPPDATA%\Programs\Python\Python312\Scripts"
call "%VCVARS%" >nul || exit /b 1
set "PATH=%LLVM_BIN%;%NINJA_BIN%;%PATH%"
python "%PROJECT_ROOT%\tools\native_gpu\p2_inject_exits.py" || exit /b 1
cmake --build "%PROJECT_ROOT%\out\build\win-amd64-Release" || exit /b 1
python "%PROJECT_ROOT%\tools\native_gpu\p2_inject_exits.py" --check || exit /b 1
echo === census build done ===
