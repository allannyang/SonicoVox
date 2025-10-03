@echo off
REM SonicoVox Debug Build + Run Script
echo Building SonicoVox (Debug)...
cmake --build build --config Debug
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ✗ Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo ✓ Debug build successful!
echo.
echo Launching ring overlay (Debug mode)...
echo Press Ctrl+Shift+V to close the overlay
echo.

start "" "build\overlays\ring\Debug\sv_overlay_ring.exe"
