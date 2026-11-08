@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
"C:\Users\DirkSommerfeld\.platformio\penv\Scripts\python.exe" tools\prepare_machine_gateway_test.py
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 test\machine_physical_promotion_test.cpp /Fe:.pio\machine_physical_promotion_test.exe /Fo:.pio\
if errorlevel 1 exit /b 1
for %%b in (cynus move) do for %%c in (white black) do for %%h in (software cable) do for %%s in (quiet capture) do for %%p in (Q R B N) do (
  .pio\machine_physical_promotion_test.exe %%b %%c %%h %%s %%p accept
  if not "!errorlevel!"=="0" exit /b 1
)
for %%b in (cynus move) do for %%h in (software cable) do for %%m in (reject blink slowblink silence stale reset disconnect subscription motorfail unexpected wronghost clear) do (
  .pio\machine_physical_promotion_test.exe %%b white %%h capture Q %%m
  if not "!errorlevel!"=="0" exit /b 1
)
for %%b in (cynus move) do (
  .pio\machine_physical_promotion_test.exe %%b white cable capture Q cablefail
  if not "!errorlevel!"=="0" exit /b 1
)
exit /b 0
