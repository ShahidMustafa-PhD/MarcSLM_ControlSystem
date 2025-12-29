#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build and package VolMarc_v3 installer
.DESCRIPTION
    Configures CMake, builds the application, and creates NSIS installer
.PARAMETER BuildType
    Build configuration: Release or Debug (default: Release)
.EXAMPLE
    .\build_installer.ps1 -BuildType Release
#>

param(
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  VolMarc_v3 Installer Build Script" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Build Type: $BuildType" -ForegroundColor Yellow
Write-Host ""

# Check if CMake is installed
try {
    $cmakeVersion = cmake --version
    Write-Host "CMake found:" -ForegroundColor Green
    Write-Host $cmakeVersion[0]
} catch {
    Write-Host "ERROR: CMake not found. Please install CMake 3.16 or later." -ForegroundColor Red
    exit 1
}

# Check if NSIS is installed
try {
    $nsisVersion = makensis /VERSION
    Write-Host "NSIS found" -ForegroundColor Green
} catch {
    Write-Host "WARNING: NSIS not found. Installer generation will be skipped." -ForegroundColor Yellow
    Write-Host "Install from: https://nsis.sourceforge.io/" -ForegroundColor Yellow
}

# Create build directory
if (-not (Test-Path "build")) {
    Write-Host "Creating build directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path "build" -Force | Out-Null
}

# Configure CMake
Write-Host ""
Write-Host "Configuring CMake..." -ForegroundColor Yellow
Push-Location "build"
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=$BuildType ..
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

# Build application
Write-Host ""
Write-Host "Building application..." -ForegroundColor Yellow
cmake --build . --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

# Create installer
Write-Host ""
Write-Host "Building installer..." -ForegroundColor Yellow
cpack -G NSIS -C $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Installer creation failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Installer build completed successfully!" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "The installer file will be located in: build\VolMarc_v3-*.exe" -ForegroundColor Green
Write-Host ""
Write-Host "To distribute the application, share the generated .exe file" -ForegroundColor Cyan
Write-Host "with users. They can run it to install VolMarc_v3." -ForegroundColor Cyan
Write-Host ""
Write-Host "Installation directory: C:\Program Files\VolMarc_v3\" -ForegroundColor Cyan
Write-Host ""
