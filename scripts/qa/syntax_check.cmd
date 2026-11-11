@echo off
rem syntax_check.cmd - MSVC syntax-only check (/Zs: no output files, safe to run
rem                    in parallel with other checks and with the real build)
rem
rem Usage: scripts\syntax_check.cmd src\gui\SomeFile.cpp [more files...]
rem        exit code 0 = syntax OK, non-zero = compiler diagnostics printed
rem
rem Why: the real build directory is single-writer (Ninja), so when several agents
rem work on different files in parallel they must not run cmake --build at the same
rem time.  /Zs parses and type-checks without writing .obj/.pdb, which is enough to
rem catch include/typo/API errors before integration.
setlocal
set "VCVARS=D:\Visual Studio\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"
set "QTDIR=C:\Qt\6.11.2\msvc2022_64"
set "REPO=%~dp0.."
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo [syntax_check] cannot initialise MSVC environment: %VCVARS%
    exit /b 2
)
cl /nologo /Zs /std:c++20 /Zc:__cplusplus /EHsc /utf-8 /permissive- /W3 ^
   /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DUNICODE /D_UNICODE ^
   /I"%REPO%\src" ^
   /I"%REPO%\third_party\OpenCL-Headers" ^
   /I"%REPO%\build\generated" ^
   /I"%QTDIR%\include" ^
   /I"%QTDIR%\include\QtCore" ^
   /I"%QTDIR%\include\QtGui" ^
   /I"%QTDIR%\include\QtWidgets" ^
   %*
exit /b %ERRORLEVEL%
