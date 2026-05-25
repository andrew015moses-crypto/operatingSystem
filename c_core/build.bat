@echo off
:: ============================================================
::  build.bat  —  Build the main EduOS simulator
::  Run this from inside the c_core\ folder in PowerShell or CMD:
::     .\build.bat
:: ============================================================

echo Building EduOS (Windows)...

gcc -Wall -Wextra -std=c11 ^
    main_sim.c ^
    process_manager.c ^
    thread_manager.c ^
    many_to_one.c ^
    ipc_module.c ^
    -o eduos.exe ^
    -I. ^
    2>&1

if %ERRORLEVEL% == 0 (
    echo.
    echo ============================================================
    echo  Build SUCCESSFUL  -  run with:   .\eduos.exe
    echo ============================================================
) else (
    echo.
    echo BUILD FAILED - check errors above
)
