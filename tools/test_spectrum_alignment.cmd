@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
for %%T in (certabo_spectrum_test certabo_protocol_test certabo_king_filter_test certabo_king_echo_test certabo_castling_test certabo_startup_regression_test certabo_queen_layout_test certabo_early_choice_test) do (
 cl /nologo /EHsc /std:c++14 /W4 /Iinclude test\%%T.cpp src\certabo_protocol.cpp /Fe:.pio\%%T.exe /Fo:.pio\
 if errorlevel 1 exit /b 1
 .pio\%%T.exe
 if errorlevel 1 exit /b 1
)
