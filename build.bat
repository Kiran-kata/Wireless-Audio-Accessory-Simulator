@echo off
REM Build script for Windows

echo === Wireless Audio Accessory Simulator Build Script ===
echo.

REM Create build directory
if not exist build mkdir build

cd build

REM Configure with CMake
echo Configuring with CMake...
cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo CMake configuration failed. Make sure CMake and Visual Studio are installed.
    cmake .. -G "MinGW Makefiles"
)

REM Build
echo Building...
cmake --build . --config Release

echo.
echo === Build Complete ===
echo.
echo Executables:
echo   .\build\Release\accessory_simulator.exe
echo   .\build\Release\host_daemon.exe
echo.
echo To run:
echo   Terminal 1: .\build\Release\accessory_simulator.exe
echo   Terminal 2: .\build\Release\host_daemon.exe
echo.

cd ..
