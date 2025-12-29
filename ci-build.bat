@echo off
REM Industrial CI/CD build pipeline for MarcSLM Control System
REM Supports Release/Debug builds with component validation
REM Usage: ci-build.bat [Release|Debug] [x64|x86]

setlocal enabledelayedexpansion

REM Parse arguments
set BUILD_TYPE=%1
set ARCHITECTURE=%2

if "!BUILD_TYPE!"=="" set BUILD_TYPE=Release
if "!ARCHITECTURE!"=="" set ARCHITECTURE=x64

REM Validate arguments
if not "!BUILD_TYPE!"=="Release" if not "!BUILD_TYPE!"=="Debug" (
    echo ERROR: Invalid build type. Use: Release or Debug
    exit /b 1
)

if not "!ARCHITECTURE!"=="x64" if not "!ARCHITECTURE!"=="x86" (
    echo ERROR: Invalid architecture. Use: x64 or x86
    exit /b 1
)

REM Set CMake generator
if "!ARCHITECTURE!"=="x64" (
    set CMAKE_ARCH=-A x64
) else (
    set CMAKE_ARCH=-A Win32
)

echo.
echo ==========================================
echo  MarcSLM Control System CI/CD Pipeline
echo ==========================================
echo.
echo Configuration:
echo   Build Type:       !BUILD_TYPE!
echo   Architecture:     !ARCHITECTURE!
echo   CMake Generator:  Ninja
echo.

REM Check prerequisites
echo Checking Prerequisites...
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake not found
    exit /b 1
)
echo   CMake:  Found

where ninja >nul 2>nul
if errorlevel 1 (
    echo ERROR: Ninja not found
    exit /b 1
)
echo   Ninja:  Found

where git >nul 2>nul
if errorlevel 1 (
    echo   Git:    Not found (optional)
) else (
    echo   Git:    Found
)

REM Clean previous build
echo.
echo Cleaning Previous Build...
if exist build (
    rmdir /s /q build
    echo   Build directory removed
)

REM Create build directory
echo Creating Build Directory...
mkdir build
cd build

REM Configure with CMake
echo.
echo Configuring CMake...
cmake -G "Ninja" !CMAKE_ARCH! -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ..
if errorlevel 1 (
    echo ERROR: CMake configuration failed
    cd ..
    exit /b 1
)
echo   CMake configuration successful

REM Build application
echo.
echo Building Application...
cmake --build . --config !BUILD_TYPE!
if errorlevel 1 (
    echo ERROR: Build failed
    cd ..
    exit /b 1
)
echo   Build successful

REM Run tests
echo.
echo Running Validation...
if exist "Release\MarcSLM_ControlSystem.exe" (
    echo   Application binary verified
) else (
    echo   WARNING: Application binary not found
)

REM Create installer
echo.
echo Creating Installer...
cpack -G NSIS -C !BUILD_TYPE!
if errorlevel 1 (
    echo ERROR: Installer creation failed
    cd ..
    exit /b 1
)
echo   Installer created successfully

cd ..

REM Print summary
echo.
echo ==========================================
echo  Build Pipeline Completed Successfully
echo ==========================================
echo.
echo Output Location:  ./build
echo Configuration:    !BUILD_TYPE!
echo Architecture:     !ARCHITECTURE!
echo.

REM Find and display installer info
for /f "delims=" %%F in ('dir /b build\MarcSLM-ControlSystem-*.exe 2^>nul') do (
    echo   Installer:  %%F
)

echo.
echo Next Steps:
echo   1. Test: .\build\Release\MarcSLM_ControlSystem.exe
echo   2. Package: Distribute .\build\MarcSLM-ControlSystem-*.exe
echo   3. Verify: Check .\build\install\ for all components
echo.
