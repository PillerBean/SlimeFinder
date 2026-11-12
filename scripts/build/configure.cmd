@echo off
call "D:\Visual Studio\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
set "PATH=C:\Qt\Tools\Ninja;D:\Visual Studio\Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cd /d "%~dp0..\.."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
exit /b %ERRORLEVEL%
