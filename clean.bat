@echo off
REM SonicoVox Clean Build Script
echo Cleaning build directory...
if exist build (
    rmdir /s /q build
    echo ✓ Build directory removed
) else (
    echo ! Build directory doesn't exist
)

echo.
echo Reconfiguring CMake project...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ✓ Project reconfigured successfully
    echo Run 'build.bat' or 'test.bat' to build
) else (
    echo.
    echo ✗ CMake configuration failed
    exit /b %ERRORLEVEL%
)
