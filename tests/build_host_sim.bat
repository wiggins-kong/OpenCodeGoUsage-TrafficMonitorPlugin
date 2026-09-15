@echo off
rem Build and run host_sim.exe (a minimal stand-in for TrafficMonitor).
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [ERROR] no Visual Studio with C++ tools found
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /utf-8 /W4 /I..\src host_sim.cpp /Fe:host_sim.exe
if errorlevel 1 (
    echo [ERROR] compile failed
    exit /b 1
)
echo.
host_sim.exe %*
exit /b %errorlevel%
