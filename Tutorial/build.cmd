@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\Current\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo MSBuild and the Visual Studio C++ workload were not found.
    exit /b 1
)
pushd "%~dp0"
"%MSBUILD%" Tutorial.vcxproj /nologo /m /p:Configuration=Release /p:Platform=x64 /p:OutDir=build/
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
