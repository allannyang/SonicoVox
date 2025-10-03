# SonicoVox Clean Build Script (PowerShell)
Write-Host "Cleaning build directory..." -ForegroundColor Cyan

if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
    Write-Host "Build directory removed" -ForegroundColor Green
} else {
    Write-Host "Build directory doesn't exist" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Reconfiguring CMake project..." -ForegroundColor Cyan

cmake -S . -B build -G "Visual Studio 17 2022" -A x64

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Project reconfigured successfully" -ForegroundColor Green
    Write-Host "Run '.\build.ps1' or '.\test.ps1' to build"
} else {
    Write-Host ""
    Write-Host "CMake configuration failed" -ForegroundColor Red
    exit $LASTEXITCODE
}
