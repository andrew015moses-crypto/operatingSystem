@echo off
:: ============================================================
::  build_race.bat  —  Race condition demo WITHOUT mutex
::  Expected: counter result will be WRONG (different each run)
:: ============================================================

echo Building race demo (NO mutex)...
gcc -Wall -Wextra -std=c11 -DUNSAFE_BUILD race_demo.c -o race_demo.exe -I. 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo === Running 5 times - results should differ each time ===
echo.
for /L %%i in (1,1,5) do (
    echo Run %%i:
    .\race_demo.exe | findstr "Actual"
)
