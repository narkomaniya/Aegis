@echo off
rem === Aegis build (g++ / TDM-GCC) ===
if not exist build mkdir build
g++ -std=c++17 -O2 -Wall src\*.cpp -o build\aegis.exe
if %errorlevel% == 0 (
    echo [OK] build\aegis.exe собран
) else (
    echo [ОШИБКА] сборка не удалась
)
