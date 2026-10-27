@echo off
setlocal
cd /d "%~dp0.."
python tools\audit_certabo_mono_v11.py
if errorlevel 1 exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 /Iinclude /Ioutput\diagnostics\certabo-mono-v11-20261027 test\certabo_mono_v11_audit.cpp src\certabo_protocol.cpp output\diagnostics\certabo-mono-v11-20261027\frozen_protocol.cpp /Fe:.pio\certabo_mono_v11_audit.exe /Fo:.pio\
if errorlevel 1 exit /b 1
.pio\certabo_mono_v11_audit.exe
