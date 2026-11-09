@echo off
rem build_repo.cmd - import the MSVC environment, put ninja/cmake on PATH, then build the repo.
rem Usage: scripts\build_repo.cmd [extra cmake --build args...]   (ASCII only; paths derive from %~dp0)
setlocal
cd /d "%~dp0.."
set "VCVARS=D:\Visual Studio\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKEDIR=D:\Visual Studio\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJADIR=C:\Qt\Tools\Ninja"
call "%VCVARS%" >nul 2>&1
if errorlevel 1 echo [build] vcvars64 failed & exit /b 1
set "PATH=%NINJADIR%;%CMAKEDIR%;%PATH%"
cmake --build "%~dp0..\build" %*
exit /b %ERRORLEVEL%
