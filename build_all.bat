@echo off
setlocal

rem ================================================================
rem  Ensure Visual Studio build environment is loaded automatically
rem ================================================================

set SCRIPT_DIR=%~dp0

rem Try to find vswhere
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo ERROR: vswhere.exe not found.
    echo Please install Visual Studio or ensure vswhere is available.
    exit /b 1
)

rem Find the latest Visual Studio installation with VC tools
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set VSINSTALL=%%i
)

if not defined VSINSTALL (
    echo ERROR: Could not find a Visual Studio installation with C++ tools.
    exit /b 1
)

rem Build full path to vcvarsall
set VCVARSALL=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat

if not exist "%VCVARSALL%" (
    echo ERROR: vcvarsall.bat not found at "%VCVARSALL%"
    exit /b 1
)

echo Loading Visual Studio environment from:
echo   %VCVARSALL%
call "%VCVARSALL%" x86_amd64 >nul

echo Visual Studio environment ready.
echo.

msbuild "%SCRIPT_DIR%solutions\naive\naive.sln" /p:Configuration=Release
msbuild "%SCRIPT_DIR%solutions\naive_plus\naive_plus.sln" /p:Configuration=Release
msbuild "%SCRIPT_DIR%solutions\markusaksli_default\markusaksli_default.sln" /p:Configuration=Release
msbuild "%SCRIPT_DIR%solutions\markusaksli_fast\markusaksli_fast.sln" /p:Configuration=Release
msbuild "%SCRIPT_DIR%solutions\markusaksli_default_threaded\markusaksli_default_threaded.sln" /p:Configuration=Release
msbuild "%SCRIPT_DIR%solutions\markusaksli_fast_threaded\markusaksli_fast_threaded.sln" /p:Configuration=Release

endlocal
pause