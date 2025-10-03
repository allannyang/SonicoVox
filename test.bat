@echo off
REM SonicoVox Build + Run Script
echo Building SonicoVox (Release)...
cmake --build build --config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ✗ Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo ✓ Build successful!
echo.
echo Launching ring overlay...
echo Press Ctrl+Shift+V to close the overlay
echo.

start "" "build\overlays\ring\Release\sv_overlay_ring.exe"
