@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
"C:\Users\DirkSommerfeld\.platformio\penv\Scripts\python.exe" tools\prepare_machine_gateway_test.py
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++14 /W4 test\machine_virtual_operator_test.cpp /Fe:.pio\machine_virtual_operator_test.exe /Fo:.pio\
if errorlevel 1 exit /b 1
for %%s in (physical_robot_capture physical_move_robot_capture physical_robot_ep physical_move_robot_ep physical_robot_castle_long physical_move_robot_castle_long physical_cynus_stale_oneshot physical_move_stale_oneshot physical_move_robot_castle ack photo_qa5_stale_d6 photo_qa5_stale_d6_idle queen_a1_no_clear queen_a1_no_clear_reverse queen_a1_no_clear_early queen_a1_no_clear_reverse_early quiet capture ep castle live confirmation disconnect fast_reply split_reply blink live_b8 reset_pending physical_position_newgame physical_soft_newgame physical_selection physical_standalone physical_physical physical_board physical_board_disconnect physical_board_setup physical_robot physical_robot_castle) do (
  .pio\machine_virtual_operator_test.exe %%s
  if not "!errorlevel!"=="0" exit /b 1
)
for %%c in (white black) do for %%e in (software cable) do for %%p in (Q R B N) do (
  .pio\machine_virtual_operator_test.exe promotion_%%c_%%e_%%p
  if not "!errorlevel!"=="0" exit /b 1
  .pio\machine_virtual_operator_test.exe promotion_%%c_%%e_capture_%%p
  if not "!errorlevel!"=="0" exit /b 1
)
for %%e in (software cable) do (
  .pio\machine_virtual_operator_test.exe promotion_white_%%e_reject_Q
  if not "!errorlevel!"=="0" exit /b 1
)
exit /b 0
