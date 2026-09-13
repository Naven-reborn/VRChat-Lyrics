@echo off
setlocal enabledelayedexpansion

REM 没传任何参数 = 双击 Explorer 启动,结尾 pause 让窗口留住。
REM 带参 = 命令行调用,不 pause。
set "INTERACTIVE=0"
if "%~1"=="" set "INTERACTIVE=1"

set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"

if /i not "%CFG%"=="Release" if /i not "%CFG%"=="Debug" (
  echo [X] Unknown config "%CFG%". Use: Release ^| Debug
  goto :end_fail
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [X] vswhere.exe not found. Install Visual Studio 2022 or VS Build Tools first.
  goto :end_fail
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo [X] No VS 2022 found with MSBuild. Open VS Installer and add "Desktop development with C++".
  goto :end_fail
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [X] vcvars64 failed
  goto :end_fail
)

cd /d "%~dp0"
set "TSTART=%TIME%"
echo.
echo === Building %CFG% x64 ============================================
echo.
msbuild vrc-lyrics.sln /p:Configuration=%CFG% /p:Platform=x64 /m /nologo /v:minimal
if errorlevel 1 goto :end_fail

set "TEND=%TIME%"

set "EXE="
for /f "delims=" %%f in ('dir /b /s "%~dp0out\%CFG%\*.exe" 2^>nul') do set "EXE=%%f"

echo.
echo === [v] Build OK ===================================================
if defined EXE (
  for %%a in ("!EXE!") do set /a "SZKB=%%~za/1024"
  echo  Output : !EXE!
  echo  Size   : !SZKB! KB
)
echo  Time   : %TSTART%  -^>  %TEND%
echo ====================================================================
goto :end_ok

:end_fail
echo.
echo === [X] Build FAILED ===============================================
if "%INTERACTIVE%"=="1" pause
endlocal
exit /b 1

:end_ok
if "%INTERACTIVE%"=="1" pause
endlocal
exit /b 0
