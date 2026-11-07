@echo off
rem === Aegis build (g++ / TDM-GCC) ===
if not exist build mkdir build
rem shell32+ole32+uuid: SHGetKnownFolderPath (правильные Desktop/Documents/Downloads)
g++ -std=c++17 -O2 -Wall src\*.cpp -o build\aegis.exe -lshell32 -lole32 -luuid
if %errorlevel% == 0 (
    echo [OK] build\aegis.exe собран
) else (
    echo [ОШИБКА] сборка не удалась
)
