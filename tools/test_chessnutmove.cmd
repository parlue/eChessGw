@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 /Itest\move_stubs /Iinclude test\chessnutmove_test.cpp src\chessnutmove_control.cpp /Fe:.pio\chessnutmove_test.exe /Fo:.pio\
if errorlevel 1 exit /b 1
.pio\chessnutmove_test.exe
