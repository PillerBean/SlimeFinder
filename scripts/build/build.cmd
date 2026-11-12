@echo off
call "D:\Visual Studio\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
set "PATH=C:\Qt\Tools\Ninja;D:\Visual Studio\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cd /d "%~dp0..\.."
cmake --build build
exit /b %ERRORLEVEL%
