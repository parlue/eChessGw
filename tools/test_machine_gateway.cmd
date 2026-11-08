@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
"C:\Users\DirkSommerfeld\.platformio\penv\Scripts\python.exe" tools\prepare_machine_gateway_test.py
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 test\machine_gateway_test.cpp /Fe:.pio\machine_gateway_test.exe /Fo:.pio\
if errorlevel 1 exit /b 1
for %%s in (robot_capture move_robot_capture robot_ep move_robot_ep robot_castle_long move_robot_castle_long cynus_stale_oneshot move_stale_oneshot move_robot_castle position_newgame soft_newgame selection standalone physical white black capture ep castle reconnect splash early reset board board_disconnect virtual_cutoff robot board_setup robot_castle phoenix_echo phoenix_echo_return phoenix_opponent_echo phoenix_echo_white phoenix_photo_capture phoenix_photo_capture_pause phoenix_quiet_f8d6 phoenix_quiet_f8d6_pause phoenix_live_stale_echo raw_trace) do (
  .pio\machine_gateway_test.exe %%s
  if not "!errorlevel!"=="0" exit /b 1
)
