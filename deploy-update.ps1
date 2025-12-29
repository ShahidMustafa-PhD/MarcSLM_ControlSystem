#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Deploy MarcControl.dll update to remote computer
.DESCRIPTION
    Copies the updated MarcControl.dll to the remote PC's updates folder
    User restarts the application to apply the update
.PARAMETER RemotePC
    Remote computer hostname or IP address
.PARAMETER DllPath
    Path to the new MarcControl.dll (default: build\bin\MarcControl.dll)
.PARAMETER InstallPath
    Remote installation path (default: C:\Program Files\MarcSLM-ControlSystem)
.PARAMETER Credential
    Credentials for remote access (optional)
.EXAMPLE
    .\deploy-update.ps1 -RemotePC "192.168.1.100"
.EXAMPLE
    .\deploy-update.ps1 -RemotePC "WORKSHOP-PC" -DllPath "custom\path\MarcControl.dll"
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$RemotePC,
    
    [string]$DllPath = "build\bin\MarcControl.dll",
    
    [string]$InstallPath = "C:\Program Files\MarcSLM-ControlSystem",
    
    [PSCredential]$Credential
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " MarcControl.dll Update Deployment" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Verify local DLL exists
if (-not (Test-Path $DllPath)) {
    Write-Host "ERROR: DLL not found at: $DllPath" -ForegroundColor Red
    Write-Host "Please build the project first:" -ForegroundColor Yellow
    Write-Host "  .\ci-build.ps1 -BuildType Release" -ForegroundColor Gray
    exit 1
}

$dllInfo = Get-Item $DllPath
Write-Host "Local DLL:" -ForegroundColor Cyan
Write-Host "  Path: $($dllInfo.FullName)"
Write-Host "  Size: $([math]::Round($dllInfo.Length/1MB, 2)) MB"
Write-Host "  Modified: $($dllInfo.LastWriteTime)"
Write-Host ""

# Construct remote path
$remotePath = "\\$RemotePC\$($InstallPath.Replace(':', '$'))\bin\updates\MarcControl.dll.new"

Write-Host "Remote Target:" -ForegroundColor Cyan
Write-Host "  Computer: $RemotePC"
Write-Host "  Path: $remotePath"
Write-Host ""

# Test remote connectivity
Write-Host "Testing connectivity to $RemotePC..." -ForegroundColor Yellow
if (-not (Test-Connection -ComputerName $RemotePC -Count 1 -Quiet)) {
    Write-Host "ERROR: Cannot reach $RemotePC" -ForegroundColor Red
    Write-Host "Please check:" -ForegroundColor Yellow
    Write-Host "  - Remote PC is powered on"
    Write-Host "  - Network connectivity"
    Write-Host "  - Firewall settings"
    exit 1
}
Write-Host "? Remote PC is reachable" -ForegroundColor Green
Write-Host ""

# Copy DLL
try {
    Write-Host "Deploying update..." -ForegroundColor Yellow
    
    if ($Credential) {
        Copy-Item $DllPath $remotePath -Credential $Credential -Force
    } else {
        Copy-Item $DllPath $remotePath -Force
    }
    
    Write-Host "? Update deployed successfully!" -ForegroundColor Green
    Write-Host ""
    
    # Verify remote file
    $remoteFileSize = (Get-Item $remotePath).Length
    if ($remoteFileSize -eq $dllInfo.Length) {
        Write-Host "? File size verified: $([math]::Round($remoteFileSize/1MB, 2)) MB" -ForegroundColor Green
    } else {
        Write-Host "WARNING: File size mismatch!" -ForegroundColor Yellow
        Write-Host "  Local: $($dllInfo.Length) bytes"
        Write-Host "  Remote: $remoteFileSize bytes"
    }
    
} catch {
    Write-Host "ERROR: Deployment failed" -ForegroundColor Red
    Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "Possible causes:" -ForegroundColor Yellow
    Write-Host "  - Insufficient permissions (try running as Administrator)"
    Write-Host "  - Remote folder doesn't exist"
    Write-Host "  - File is locked (close application on remote PC)"
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Deployment Complete" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Cyan
Write-Host "  1. Notify user on remote PC" -ForegroundColor Gray
Write-Host "  2. User closes MarcSLM application" -ForegroundColor Gray
Write-Host "  3. User reopens application" -ForegroundColor Gray
Write-Host "  4. New version loads automatically" -ForegroundColor Gray
Write-Host ""
Write-Host "Rollback (if needed):" -ForegroundColor Yellow
Write-Host "  - Remote PC: Delete MarcControl.dll" -ForegroundColor Gray
Write-Host "  - Remote PC: Rename MarcControl.dll.old -> MarcControl.dll" -ForegroundColor Gray
Write-Host ""
