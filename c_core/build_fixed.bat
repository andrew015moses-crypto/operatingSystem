@echo off
:: ============================================================
::  build_fixed.bat  —  Race condition demo WITH mutex
::  Expected: counter always exactly 400000
:: ============================================================

echo Building fixed demo (WITH mutex)...
gcc -Wall -Wextra -std=c11 race_demo.c -o race_fixed.exe -I. 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo === Running 3 times - should always be 400000 ===
echo.
for /L %%i in (1,1,3) do (
    echo Run %%i:
    .\race_fixed.exe | findstr "Actual"
)
