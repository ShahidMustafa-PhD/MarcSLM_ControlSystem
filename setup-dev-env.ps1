#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Set up development environment for VolMarc_v3
.DESCRIPTION
    Installs required tools, validates dependencies, and prepares build environment
.PARAMETER InstallTools
    Automatically install missing tools using Chocolatey
.PARAMETER Verbose
    Show detailed output
.EXAMPLE
    .\setup-dev-env.ps1 -InstallTools
#>

param(
    [switch]$InstallTools,
    [switch]$Verbose
)

$ErrorActionPreference = "Continue"
$VerbosePreference = if ($Verbose) { "Continue" } else { "SilentlyContinue" }

# Define required tools
$tools = @(
    @{
        Name = "CMake"
        Executable = "cmake"
        Version = "3.16+"
        Choco = "cmake"
        Url = "https://cmake.org/download/"
    },
    @{
        Name = "Ninja"
        Executable = "ninja"
        Version = "latest"
        Choco = "ninja"
        Url = "https://github.com/ninja-build/ninja/releases"
    },
    @{
        Name = "Git"
        Executable = "git"
        Version = "latest"
        Choco = "git"
        Url = "https://git-scm.com/download/win"
    },
    @{
        Name = "NSIS"
        Executable = "makensis"
        Version = "latest"
        Choco = "nsis"
        Url = "https://nsis.sourceforge.io/"
    }
)

# Define optional tools
$optionalTools = @(
    @{
        Name = "Visual Studio Code"
        Executable = "code"
        Choco = "vscode"
    },
    @{
        Name = "CMake Tools Extension"
        Note = "Install from VS Code: ms-vscode.cmake-tools"
    }
)

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VolMarc_v3 Development Environment Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check for admin privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]'Administrator')
if ($InstallTools -and -not $isAdmin) {
    Write-Host "WARNING: Administrator privileges required for installing tools" -ForegroundColor Yellow
    Write-Host "Please run this script as Administrator with -InstallTools flag" -ForegroundColor Yellow
    Write-Host ""
}

# Check required tools
Write-Host "Checking Required Tools..." -ForegroundColor Yellow
Write-Host ""

$allToolsFound = $true
foreach ($tool in $tools) {
    $found = $false
    try {
        $cmd = & $tool.Executable --version 2>$null
        if ($?) {
            Write-Host "  ? $($tool.Name)" -ForegroundColor Green
            Write-Verbose "    $cmd"
            $found = $true
        }
    } catch {
        Write-Verbose "    Error checking $($tool.Executable): $_"
    }
    
    if (-not $found) {
        Write-Host "  ? $($tool.Name) NOT FOUND" -ForegroundColor Red
        Write-Host "    Required: $($tool.Version)" -ForegroundColor Yellow
        Write-Host "    Download: $($tool.Url)" -ForegroundColor Gray
        $allToolsFound = $false
        
        if ($InstallTools -and $isAdmin) {
            Write-Host "    Installing via Chocolatey..." -ForegroundColor Cyan
            choco install $tool.Choco -y | Out-Null
            if ($?) {
                Write-Host "    ? Installed successfully" -ForegroundColor Green
            }
        }
    }
}

Write-Host ""

# Check optional tools
Write-Host "Checking Optional Tools..." -ForegroundColor Yellow
Write-Host ""

foreach ($tool in $optionalTools) {
    if ($tool.Executable) {
        try {
            & $tool.Executable --version >$null 2>&1
            if ($?) {
                Write-Host "  ? $($tool.Name)" -ForegroundColor Green
            } else {
                Write-Host "  ? $($tool.Name) (optional)" -ForegroundColor Gray
            }
        } catch {
            Write-Host "  ? $($tool.Name) (optional)" -ForegroundColor Gray
        }
    } else {
        Write-Host "  ? $($tool.Name)" -ForegroundColor Gray
        if ($tool.Note) { Write-Host "    $($tool.Note)" -ForegroundColor Gray }
    }
}

Write-Host ""

# Check environment variables
Write-Host "Checking Environment Variables..." -ForegroundColor Yellow
Write-Host ""

$envVars = @(
    @{ Name = "CMAKE_PREFIX_PATH"; Optional = $true; Note = "Path to Qt5 installation" },
    @{ Name = "NINJA_STATUS"; Optional = $true; Note = "Ninja build progress (optional)" }
)

foreach ($var in $envVars) {
    $value = [Environment]::GetEnvironmentVariable($var.Name)
    if ($value) {
        Write-Host "  ? $($var.Name)" -ForegroundColor Green
        Write-Verbose "    Value: $value"
    } else {
        $type = if ($var.Optional) { "optional" } else { "REQUIRED" }
        Write-Host "  ? $($var.Name) ($type)" -ForegroundColor Gray
        if ($var.Note) { Write-Host "    $($var.Note)" -ForegroundColor Gray }
    }
}

Write-Host ""

# Validate Qt5 installation
Write-Host "Validating Qt5 Installation..." -ForegroundColor Yellow
Write-Host ""

$qtPaths = @(
    "C:\Qt\5.15.2\msvc2019_64",
    "C:\Qt\5.15.2\msvc2019_32",
    $env:CMAKE_PREFIX_PATH
)

$qtFound = $false
foreach ($path in $qtPaths) {
    if ($path -and (Test-Path "$path\bin\qmake.exe")) {
        Write-Host "  ? Qt5 found at: $path" -ForegroundColor Green
        Write-Host "    qmake.exe is available" -ForegroundColor Gray
        $qtFound = $true
        break
    }
}

if (-not $qtFound) {
    Write-Host "  ? Qt5 not found" -ForegroundColor Red
    Write-Host "    Please set CMAKE_PREFIX_PATH to your Qt5 installation" -ForegroundColor Yellow
    Write-Host "    Example: C:\Qt\5.15.2\msvc2019_64" -ForegroundColor Gray
}

Write-Host ""

# Validate RTC5 library
Write-Host "Validating RTC5 Library..." -ForegroundColor Yellow
Write-Host ""

if (Test-Path ".\RTC5 Files\RTC5DLL.lib") {
    Write-Host "  ? RTC5 library (32-bit) found" -ForegroundColor Green
} else {
    Write-Host "  ? RTC5 library (32-bit) not found" -ForegroundColor Gray
}

if (Test-Path ".\RTC5 Files\RTC5DLLx64.lib") {
    Write-Host "  ? RTC5 library (64-bit) found" -ForegroundColor Green
} else {
    Write-Host "  ? RTC5 library (64-bit) not found" -ForegroundColor Gray
}

Write-Host ""

# Prepare environment
Write-Host "Environment Preparation..." -ForegroundColor Yellow
Write-Host ""

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" -Force | Out-Null
    Write-Host "  ? Build directory created" -ForegroundColor Green
} else {
    Write-Host "  ? Build directory exists" -ForegroundColor Green
}

Write-Host ""

# Summary
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Setup Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($allToolsFound) {
    Write-Host "? All required tools are available" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Configure: cmake -G Ninja -DCMAKE_BUILD_TYPE=Release"
    Write-Host "  2. Build:     cmake --build ."
    Write-Host "  3. Package:   cpack -G NSIS"
    Write-Host ""
    Write-Host "Or use the build script:" -ForegroundColor Cyan
    Write-Host "  .\ci-build.ps1 -BuildType Release" -ForegroundColor Gray
} else {
    Write-Host "? Some required tools are missing" -ForegroundColor Red
    Write-Host ""
    Write-Host "To install missing tools:" -ForegroundColor Yellow
    Write-Host "  1. Run as Administrator" -ForegroundColor Gray
    Write-Host "  2. Execute: .\setup-dev-env.ps1 -InstallTools" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or install manually from the URLs above" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Documentation:" -ForegroundColor Cyan
Write-Host "  • CI_CD_GUIDE.md - Complete CI/CD documentation"
Write-Host "  • INSTALLER_GUIDE.md - Installer configuration"
Write-Host "  • INSTALLER_QUICKSTART.md - Quick reference"
Write-Host ""
