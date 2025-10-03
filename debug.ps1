# SonicoVox Debug Build + Run Script (PowerShell)
Write-Host "Building SonicoVox (Debug)..." -ForegroundColor Cyan

cmake --build build --config Debug

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build failed with error code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Debug build successful!" -ForegroundColor Green
Write-Host ""
Write-Host "Launching ring overlay (Debug mode)..." -ForegroundColor Yellow
Write-Host "Press Ctrl+Shift+V to close the overlay" -ForegroundColor Gray
Write-Host ""

$exePath = "build\overlays\ring\Debug\sv_overlay_ring.exe"
Start-Process -FilePath $exePath
