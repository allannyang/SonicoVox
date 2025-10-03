# SonicoVox Build + Run Script (PowerShell)
Write-Host "Building SonicoVox (Release)..." -ForegroundColor Cyan

cmake --build build --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build failed with error code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Build successful!" -ForegroundColor Green
Write-Host ""
Write-Host "Launching ring overlay..." -ForegroundColor Yellow
Write-Host "Press Ctrl+Shift+V to close the overlay" -ForegroundColor Gray
Write-Host ""

$exePath = "build\overlays\ring\Release\sv_overlay_ring.exe"
Start-Process -FilePath $exePath -WorkingDirectory (Get-Location)
