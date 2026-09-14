@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /MT /O2 /DUNICODE /D_UNICODE Tutorial.cpp Visuals.cpp /Fo:build\ /Fe:build\AshenShore.exe /link /SUBSYSTEM:WINDOWS
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
