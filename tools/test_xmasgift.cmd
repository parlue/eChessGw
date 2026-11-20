@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /O2 /Gy /constexpr:steps10000000 /DVIRGIN_EMBEDDED=1 /DVIRGIN_EMBEDDED_MAX_PLY=20 /DVIRGIN_EMBEDDED_HASH_KIB=16 /Isrc\xmasgift\virgin test\xmasgift_test.cpp src\xmasgift\virgin\*.cpp /Fe:output\xmasgift-20261119\xmasgift_test.exe /Fo:output\xmasgift-20261119\ /link /OPT:REF
if errorlevel 1 exit /b 1
output\xmasgift-20261119\xmasgift_test.exe
exit /b %errorlevel%
