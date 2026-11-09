@echo off
rem temp_build_probe.cmd - ASCII-only probe builder for C:\Temp\slimecache\probe20
rem Usage: temp_build_probe.cmd <source.cpp>          (source in this directory)
rem Requires env: REPO_SRC = <repo>\src, REPO_LIB = <repo>\build\slime_core.lib
setlocal
call "D:\Visual Studio\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 echo [probe] vcvars64 failed & exit /b 1
cd /d "%~dp0"
set "OUT=%~dp0%~n1.exe"
cl /nologo /std:c++20 /Zc:__cplusplus /O2 /EHsc /utf-8 /permissive- /W3 /DNOMINMAX /MD /openmp /I"%REPO_SRC%" /Fe:"%OUT%" "%~dp0%~n1.cpp" "%REPO_LIB%"
exit /b %ERRORLEVEL%
