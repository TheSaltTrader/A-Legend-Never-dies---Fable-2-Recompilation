@echo off
rem Compile the native-GPU test shaders (src\ngpu_shaders\*.hlsl) to signed
rem DXIL beside the executable, with the dxc that ships in XenosRecomp's
rem third-party tree (dxil.dll next to it signs the output; D3D12 rejects
rem unsigned DXIL). Run after tools\build.cmd.
setlocal
set "ROOT=%~dp0.."
set "DXC=C:\Users\renoi\ClaudeCode\NativeGPU\reference\XenosRecomp\thirdparty\dxc-bin\bin\x64\dxc.exe"
set "OUT=%ROOT%\out\build\win-amd64-Release"
if not exist "%DXC%" (echo dxc not found at "%DXC%" & exit /b 1)
"%DXC%" -T vs_6_0 -E main -Fo "%OUT%\ngpu_vs.dxil" "%ROOT%\src\ngpu_shaders\ngpu_vs.hlsl" || exit /b 2
"%DXC%" -T ps_6_0 -E main -Fo "%OUT%\ngpu_ps.dxil" "%ROOT%\src\ngpu_shaders\ngpu_ps.hlsl" || exit /b 3
"%DXC%" -T vs_6_0 -E main -Fo "%OUT%\ngpu_blit_vs.dxil" "%ROOT%\src\ngpu_shaders\ngpu_blit_vs.hlsl" || exit /b 6
"%DXC%" -T ps_6_0 -E main -Fo "%OUT%\ngpu_ps_xs.dxil" "%ROOT%\src\ngpu_shaders\ngpu_ps_xs.hlsl" || exit /b 4
"%DXC%" -T gs_6_0 -E main -Fo "%OUT%\ngpu_rect_gs.dxil" "%ROOT%\src\ngpu_shaders\ngpu_rect_gs.hlsl" || exit /b 7
"%DXC%" -T gs_6_0 -E main -Fo "%OUT%\ngpu_point_gs.dxil" "%ROOT%\src\ngpu_shaders\ngpu_point_gs.hlsl" || exit /b 8
echo native-GPU shaders compiled to "%OUT%"
