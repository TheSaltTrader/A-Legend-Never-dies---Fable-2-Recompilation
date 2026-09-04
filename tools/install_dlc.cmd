@echo off
setlocal
rem Install Xbox 360 content packages, then run.
rem
rem   tools\install_dlc.cmd [package-folder] [extra fable2 args...]
rem
rem This is DELIBERATELY a separate script rather than something run.cmd does,
rem because for Fable II it is mostly unnecessary: Knothole Island and See the
rem Future are already on the Game of the Year disc. Their level data is in
rem data\levels.bnk (Worlds\Albion\DLC2\* and Worlds\Albion\MysteryIsland),
rem scenarios.list registers the four DLC2 levels, and the executable itself
rem contains KnotholeIslandSeasonManager, the QD010_KnotholeIsland quests and
rem the expansion achievement text. Installing the standalone packages for them
rem extracts about a gigabyte each and adds nothing.
rem
rem Run this only for a package that is genuinely NOT on the disc - the 12 KB
rem "Collectors' Edition Content" token, or anything new.
rem
rem Packages already installed are skipped, and two copies of the same package
rem (same display name, different signature bytes) install only once.

set "PROJECT_ROOT=%~dp0.."
set "DLC_ROOT=%~1"
if "%DLC_ROOT%"=="" set "DLC_ROOT=%PROJECT_ROOT%\..\DLC\4D5307F1\00000002"
if not "%~1"=="" shift

set "EXE=%PROJECT_ROOT%\out\build\win-amd64-Release\fable2.exe"
if not exist "%EXE%" (
    echo ERROR: %EXE% not found - run tools\build.cmd first
    exit /b 1
)
if not exist "%DLC_ROOT%" (
    echo ERROR: package folder "%DLC_ROOT%" not found
    exit /b 1
)

echo Installing content packages from "%DLC_ROOT%"
"%EXE%" --game_data_root "%PROJECT_ROOT%\game" ^
        --dlc_root "%DLC_ROOT%" ^
        --log_file "%PROJECT_ROOT%\out\fable2.log" ^
        --log_level debug ^
        %*
