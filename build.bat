@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo vcvars64 failed
  exit /b 1
)
cd /d "%~dp0"
msbuild vrc-lyrics.sln /p:Configuration=%1 /p:Platform=x64 /m /nologo /v:minimal
exit /b %ERRORLEVEL%
