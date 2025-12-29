@echo off
REM Deploy MarcControl.dll update to remote computer
REM Usage: deploy-update.bat <RemotePC> [DllPath]

setlocal enabledelayedexpansion

set REMOTE_PC=%1
set DLL_PATH=%2
set INSTALL_PATH=C:\Program Files\MarcSLM-ControlSystem

if "!REMOTE_PC!"=="" (
    echo ERROR: Remote PC not specified
    echo.
    echo Usage: deploy-update.bat ^<RemotePC^> [DllPath]
    echo.
    echo Example:
    echo   deploy-update.bat 192.168.1.100
    echo   deploy-update.bat WORKSHOP-PC build\bin\MarcControl.dll
    exit /b 1
)

if "!DLL_PATH!"=="" (
    set DLL_PATH=build\bin\MarcControl.dll
)

echo.
echo ========================================
echo  MarcControl.dll Update Deployment
echo ========================================
echo.

REM Verify local DLL exists
if not exist "!DLL_PATH!" (
    echo ERROR: DLL not found at: !DLL_PATH!
    echo.
    echo Please build the project first:
    echo   ci-build.bat Release
    exit /b 1
)

echo Local DLL: !DLL_PATH!
echo Remote PC: !REMOTE_PC!
echo.

REM Test connectivity
echo Testing connectivity...
ping !REMOTE_PC! -n 1 >nul 2>nul
if errorlevel 1 (
    echo ERROR: Cannot reach !REMOTE_PC!
    echo.
    echo Please check:
    echo   - Remote PC is powered on
    echo   - Network connectivity
    echo   - Firewall settings
    exit /b 1
)
echo Remote PC is reachable
echo.

REM Construct remote path
set REMOTE_PATH=\\!REMOTE_PC!\C$\!INSTALL_PATH:C:\=!\bin\updates\MarcControl.dll.new

echo Deploying to: !REMOTE_PATH!
echo.

REM Copy DLL
copy /Y "!DLL_PATH!" "!REMOTE_PATH!" >nul 2>nul
if errorlevel 1 (
    echo ERROR: Deployment failed
    echo.
    echo Possible causes:
    echo   - Insufficient permissions (try running as Administrator)
    echo   - Remote folder doesn't exist
    echo   - File is locked (close application on remote PC)
    exit /b 1
)

echo Deployment successful!
echo.

echo ========================================
echo  Deployment Complete
echo ========================================
echo.
echo Next Steps:
echo   1. Notify user on remote PC
echo   2. User closes MarcSLM application
echo   3. User reopens application
echo   4. New version loads automatically
echo.
echo Rollback (if needed):
echo   - Remote PC: Delete MarcControl.dll
echo   - Remote PC: Rename MarcControl.dll.old -^> MarcControl.dll
echo.
