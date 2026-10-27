@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 /Iinclude test\certabo_startup_regression_test.cpp src\certabo_protocol.cpp /Fe:.pio\certabo_startup_regression_test.exe /Fo:.pio\
if errorlevel 1 exit /b 1
.pio\certabo_startup_regression_test.exe
