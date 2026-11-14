@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
"C:\Users\DirkSommerfeld\.platformio\penv\Scripts\python.exe" tools\prepare_machine_gateway_test.py
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 /DTEST_DEFAULT_VIRTUAL_PROMOTION test\machine_virtual_operator_test.cpp /Fe:.pio\machine_virtual_promotion_default_test.exe /Fo:.pio\
if errorlevel 1 exit /b 1
for %%c in (white black) do for %%e in (software cable) do for %%p in (Q R B N) do (
  .pio\machine_virtual_promotion_default_test.exe promotion_%%c_%%e_%%p
  if not "!errorlevel!"=="0" exit /b 1
  .pio\machine_virtual_promotion_default_test.exe promotion_%%c_%%e_capture_%%p
  if not "!errorlevel!"=="0" exit /b 1
)
for %%e in (software cable) do (
  .pio\machine_virtual_promotion_default_test.exe promotion_white_%%e_reject_Q
  if not "!errorlevel!"=="0" exit /b 1
)
exit /b 0
