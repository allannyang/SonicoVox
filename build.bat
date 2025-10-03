@echo off
REM SonicoVox Build Script
echo Building SonicoVox (Release)...
cmake --build build --config Release
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ✓ Build successful!
    echo Executable: build\overlays\ring\Release\sv_overlay_ring.exe
) else (
    echo.
    echo ✗ Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
