# SonicoVox Build Script (PowerShell)
Write-Host "Building SonicoVox (Release)..." -ForegroundColor Cyan

cmake --build build --config Release

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host "Executable: build\overlays\ring\Release\sv_overlay_ring.exe"
} else {
    Write-Host ""
    Write-Host "Build failed with error code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
