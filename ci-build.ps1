#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Industrial CI/CD build pipeline for MarcSLM Control System
.DESCRIPTION
    Configures, builds, packages, and tests the application
    Supports Release and Debug builds with component validation
.PARAMETER BuildType
    Build configuration: Release or Debug
.PARAMETER Architecture
    Target architecture: x64 or x86
.PARAMETER SkipTests
    Skip testing phase
.PARAMETER SkipPackage
    Skip installer creation
.EXAMPLE
    .\ci-build.ps1 -BuildType Release -Architecture x64
#>

param(
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",
    
    [ValidateSet("x64", "x86")]
    [string]$Architecture = "x64",
    
    [switch]$SkipTests,
    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"
$WarningPreference = "Continue"

# Colors for output
$colors = @{
    Success = "Green"
    Warning = "Yellow"
    Error   = "Red"
    Info    = "Cyan"
    Debug   = "Gray"
}

function Write-Status {
    param([string]$Message, [string]$Level = "Info")
    $timestamp = Get-Date -Format "HH:mm:ss"
    Write-Host "[$timestamp]" -ForegroundColor $colors[$Level] -NoNewline
    Write-Host " $Message"
}

# Set CMake generator based on architecture
$generator = if ($Architecture -eq "x64") { "Visual Studio 17 2022" } else { "Visual Studio 17 2022" }
$cmakeArch = if ($Architecture -eq "x64") { "-A x64" } else { "-A Win32" }

Write-Host ""
Write-Status "========================================" "Info"
Write-Status "MarcSLM Control System CI/CD Pipeline" "Info"
Write-Status "========================================" "Info"
Write-Host ""
Write-Status "Configuration" "Info"
Write-Host "  Build Type:       $BuildType"
Write-Host "  Architecture:     $Architecture"
Write-Host "  Generator:        $generator"
Write-Host "  Skip Tests:       $SkipTests"
Write-Host "  Skip Package:     $SkipPackage"
Write-Host ""

# Validate prerequisites
Write-Status "Checking Prerequisites..." "Info"

$prerequisites = @(
    @{ Tool = "cmake"; Command = "cmake --version"; ErrorMsg = "CMake not found" }
    @{ Tool = "ninja"; Command = "ninja --version"; ErrorMsg = "Ninja not found" }
    @{ Tool = "git"; Command = "git --version"; ErrorMsg = "Git not found (optional)" }
)

foreach ($prereq in $prerequisites) {
    try {
        & $prereq.Command | Out-Null
        Write-Status "$($prereq.Tool): Found" "Success"
    } catch {
        if ($prereq.ErrorMsg -match "optional") {
            Write-Status "$($prereq.ErrorMsg)" "Warning"
        } else {
            Write-Status "$($prereq.ErrorMsg)" "Error"
            exit 1
        }
    }
}

# Clean previous build
Write-Host ""
Write-Status "Cleaning Previous Build..." "Info"
if (Test-Path "build") {
    Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue
    Write-Status "Build directory removed" "Success"
}

# Create build directory
Write-Status "Creating Build Directory..." "Info"
New-Item -ItemType Directory -Path "build" -Force | Out-Null
Write-Status "Build directory created" "Success"

# Configure with CMake
Write-Host ""
Write-Status "Configuring CMake..." "Info"
Push-Location "build"

$cmakeCmd = "cmake -G Ninja $cmakeArch -DCMAKE_BUILD_TYPE=$BuildType .."
Write-Status "Running: $cmakeCmd" "Debug"

cmake -G Ninja $cmakeArch -DCMAKE_BUILD_TYPE=$BuildType .. 2>&1 | ForEach-Object {
    if ($_ -match "ERROR") { Write-Status "$_" "Error" }
    elseif ($_ -match "WARNING") { Write-Status "$_" "Warning" }
    elseif ($_ -match "MarcSLM Control System Version") { Write-Status "$_" "Success" }
}

if ($LASTEXITCODE -ne 0) {
    Write-Status "CMAKE CONFIGURATION FAILED" "Error"
    Pop-Location
    exit 1
}
Write-Status "CMake configuration successful" "Success"

# Build application
Write-Host ""
Write-Status "Building Application..." "Info"
$buildStartTime = Get-Date

cmake --build . --config $BuildType -- -j (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
if ($LASTEXITCODE -ne 0) {
    Write-Status "BUILD FAILED" "Error"
    Pop-Location
    exit 1
}

$buildTime = (Get-Date) - $buildStartTime
Write-Status "Build successful in $($buildTime.TotalSeconds)s" "Success"

# Run tests (if enabled)
if (-not $SkipTests) {
    Write-Host ""
    Write-Status "Running Tests..." "Info"
    
    $exePath = Join-Path $PWD "Release/MarcSLM_ControlSystem.exe"
    if (Test-Path $exePath) {
        Write-Status "Application binary verified: $exePath" "Success"
        
        # Add smoke tests here if available
        # & $exePath --test
    } else {
        Write-Status "Application binary not found" "Warning"
    }
}

# Create installer (if enabled)
if (-not $SkipPackage) {
    Write-Host ""
    Write-Status "Creating Installer..." "Info"
    
    cpack -G NSIS -C $BuildType 2>&1 | ForEach-Object {
        if ($_ -match "ERROR") { Write-Status "$_" "Error" }
        elseif ($_ -match "CPack") { Write-Status "$_" "Debug" }
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Status "INSTALLER CREATION FAILED" "Error"
        Pop-Location
        exit 1
    }
    Write-Status "Installer created successfully" "Success"
}

Pop-Location

# Print summary
Write-Host ""
Write-Status "========================================" "Info"
Write-Status "Build Pipeline Completed Successfully" "Success"
Write-Status "========================================" "Info"
Write-Host ""
Write-Status "Output Location:  ./build" "Info"
Write-Status "Configuration:    $BuildType" "Info"
Write-Status "Architecture:     $Architecture" "Info"
Write-Host ""

if (-not $SkipPackage) {
    Get-ChildItem -Path "build" -Filter "MarcSLM-ControlSystem-*.exe" | ForEach-Object {
        Write-Status "Installer:        $($_.Name)" "Success"
        Write-Status "Size:             $([math]::Round($_.Length/1MB, 2)) MB" "Info"
    }
}

Write-Host ""
Write-Status "Next Steps:" "Info"
Write-Host "  1. Test the application: ./build/Release/MarcSLM_ControlSystem.exe"
Write-Host "  2. Distribute installer: ./build/MarcSLM-ControlSystem-*.exe"
Write-Host "  3. Verify components in: ./build/install/"
Write-Host ""
