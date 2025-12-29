# VolMarc_v3 - Industrial CI/CD Pipeline Documentation

## Overview

This document describes the CI/CD-ready build system for VolMarc_v3, designed for automated continuous integration, testing, and deployment workflows.

### Architecture

```
CMakeLists.txt (root)
??? cmake/
?   ??? Version.cmake         (Centralized version management)
?   ??? installer/
?       ??? CMakeLists.txt    (Modular packaging configuration)
??? ci-build.ps1             (PowerShell CI/CD script)
??? ci-build.bat             (Batch CI/CD script)
??? [Application sources...]
```

### Key Features

? **Modular Architecture** - Installer as separate CMake subdirectory  
? **Component-Based Packaging** - Install only required components  
? **Version Management** - Centralized, CI/CD-aware versioning  
? **Multi-Architecture Support** - x86 and x64 automatic detection  
? **Automated Builds** - Single-command compilation and packaging  
? **Git Integration** - Automatic commit hash in version strings  

---

## Quick Start

### Local Build (Development)

#### PowerShell
```powershell
.\ci-build.ps1 -BuildType Release -Architecture x64
```

#### Command Prompt
```batch
ci-build.bat Release x64
```

### Build Output

```
build/
??? Release/                          # Compiled binaries
?   ??? VolMarc_v3.exe              # Main executable
??? install/                          # Staged installation (for testing)
??? VolMarc_v3-4.1.0-win64.exe      # Final installer
??? CMakeFiles/                       # Build system files
```

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: CI/CD Pipeline

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
  release:
    types: [created]

jobs:
  build:
    runs-on: windows-latest
    strategy:
      matrix:
        build-type: [Release, Debug]
        architecture: [x64, x86]
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install Dependencies
        run: |
          choco install cmake ninja -y
      
      - name: Build
        run: .\ci-build.ps1 -BuildType ${{ matrix.build-type }} -Architecture ${{ matrix.architecture }}
      
      - name: Upload Artifacts
        uses: actions/upload-artifact@v3
        with:
          name: VolMarc_v3-${{ matrix.build-type }}-${{ matrix.architecture }}
          path: build/VolMarc_v3-*.exe
          retention-days: 30
```

### GitLab CI Example

```yaml
variables:
  CMAKE_VERSION: "3.16"
  NINJA_VERSION: "latest"

stages:
  - build
  - test
  - package
  - deploy

build_x64_release:
  stage: build
  image: mcr.microsoft.com/windows/servercore:ltsc2022
  script:
    - .\ci-build.ps1 -BuildType Release -Architecture x64
  artifacts:
    paths:
      - build/
    expire_in: 1 week

build_x86_release:
  stage: build
  image: mcr.microsoft.com/windows/servercore:ltsc2022
  script:
    - .\ci-build.ps1 -BuildType Release -Architecture x86
  artifacts:
    paths:
      - build/
    expire_in: 1 week
```

---

## Version Management

### Version File Location

`cmake/Version.cmake` - Central version configuration

### Version Format

**Semantic Versioning**: `MAJOR.MINOR.PATCH[-BUILD][+GIT_HASH]`

Examples:
- `4.1.0` - Release version
- `4.1.0-rc.1` - Release candidate
- `4.1.0-beta.1` - Beta release
- `4.1.0+git.a1b2c3d` - Development build with Git hash

### Updating Version

Edit `cmake/Version.cmake`:

```cmake
set(VOLMARC_VERSION_MAJOR 4)
set(VOLMARC_VERSION_MINOR 1)
set(VOLMARC_VERSION_PATCH 0)
set(VOLMARC_VERSION_BUILD "rc.1")  # Optional
```

Rebuilding automatically includes the new version in:
- Executable version resource
- Installer filename
- About dialog

---

## Component Management

### Installation Components

| Component | Required | Contents |
|-----------|----------|----------|
| **runtime** | Yes | Main executable |
| **libraries** | Yes | Qt5 DLLs, RTC5 DLL, MSVC runtime |
| **plugins** | Yes | Qt platform & image format plugins |
| **data** | No | RTC5Dat.dat, configuration files |
| **documentation** | No | README, LICENSE, user guides |

### Component Installation Paths

```
C:\Program Files\VolMarc_v3\
??? bin/                           # runtime, libraries, plugins
?   ??? VolMarc_v3.exe           # Main app
?   ??? Qt5*.dll                 # Qt libraries
?   ??? RTC5*.dll                # Scanner support
?   ??? platforms/               # plugins
?   ?   ??? qwindows.dll
?   ??? imageformats/            # plugins
?   ?   ??? *.dll
?   ??? styles/                  # plugins
??? data/                          # data component
?   ??? RTC5Dat.dat
??? doc/                          # documentation
?   ??? LICENSE.txt
?   ??? README.md
?   ??? INSTALL.md
??? config/                        # user configuration
    ??? defaults.json
```

### Custom Component Installation

Users can select components during installation:

```nsis
; Installer allows unchecking optional components
Components
  - "Application (required)"       [forced]
  - "Libraries (required)"         [forced]
  - "Qt Plugins (required)"        [forced]
  - "Data Files"                   [optional]
  - "Documentation"                [optional]
```

---

## Build Variations

### Release Build (Production)

```bash
ci-build.ps1 -BuildType Release -Architecture x64
```

**Characteristics:**
- Optimizations enabled (`/O2`)
- No debug symbols
- Smaller installer
- Production-ready

### Debug Build (Development)

```bash
ci-build.ps1 -BuildType Debug -Architecture x64
```

**Characteristics:**
- Full debug symbols
- Optimizations disabled
- Larger executable
- Debugging enabled

### 32-bit vs 64-bit

```bash
# 64-bit (default)
ci-build.ps1 -BuildType Release -Architecture x64

# 32-bit
ci-build.ps1 -BuildType Release -Architecture x86
```

Installer filename automatically reflects architecture:
- 64-bit: `VolMarc_v3-4.1.0-win64.exe`
- 32-bit: `VolMarc_v3-4.1.0-win32.exe`

---

## Installer Signing (Optional)

### Enable Code Signing

Create `cmake/installer/sign.cmake`:

```cmake
find_program(SIGNTOOL signtool)

if(SIGNTOOL)
    add_custom_command(TARGET package POST_BUILD
        COMMAND ${SIGNTOOL} sign
            /f "$ENV{CERT_FILE}"
            /p "$ENV{CERT_PASSWORD}"
            /t "http://timestamp.server.com"
            "${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.exe"
        COMMENT "Signing installer with certificate"
    )
endif()
```

### CI/CD Environment Variables

```bash
export CERT_FILE=/path/to/cert.pfx
export CERT_PASSWORD=your-password
```

---

## Troubleshooting

### Common Issues

#### CMake Configuration Fails

**Error**: `CMake not found`

**Solution**:
```powershell
choco install cmake -y
# or
winget install CMake.CMake
```

#### Ninja Build System Missing

**Error**: `Ninja not found`

**Solution**:
```powershell
choco install ninja -y
# or
winget install Ninja-build.Ninja
```

#### Qt Libraries Not Found

**Error**: `Qt5 not found`

**Solution**:
1. Verify Qt5 installation directory
2. Set `CMAKE_PREFIX_PATH`:
```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\5.15.2\msvc2019_64"
.\ci-build.ps1 -BuildType Release
```

#### Installer Generation Fails

**Error**: `NSIS not found`

**Solution**:
```powershell
choco install nsis -y
```

---

## Performance Optimization

### Parallel Build

Automatically uses all CPU cores. To override:

```powershell
# Edit ci-build.ps1 or ci-build.bat
cmake --build . --config Release -- -j 4
```

### Incremental Builds

Skip clean step for faster rebuilds:

```powershell
cd build
cmake --build . --config Release
cpack -G NSIS
```

---

## Testing & Validation

### Pre-Release Checklist

- [ ] Build in Release mode on all architectures
- [ ] Verify installer executes without errors
- [ ] Test application startup and UI rendering
- [ ] Confirm all dependencies are packaged
- [ ] Check version strings in About dialog
- [ ] Validate installer uninstall process

### Automated Testing

Add to CI pipeline:

```powershell
# Smoke test
$exePath = "build/Release/VolMarc_v3.exe"
if (Test-Path $exePath) {
    Write-Host "Binary verification: PASS"
} else {
    Write-Host "Binary verification: FAIL"
    exit 1
}

# List installer
Get-ChildItem build/ -Filter "*.exe" | ForEach-Object {
    Write-Host "Generated: $($_.Name) ($([math]::Round($_.Length/1MB, 2)) MB)"
}
```

---

## Release Workflow

### Release Steps

1. **Update Version**
   ```
   cmake/Version.cmake
   - Update MAJOR.MINOR.PATCH
   ```

2. **Build Release**
   ```powershell
   .\ci-build.ps1 -BuildType Release -Architecture x64
   .\ci-build.ps1 -BuildType Release -Architecture x86
   ```

3. **Test Installers**
   - Run each .exe in clean VM
   - Verify functionality
   - Check shortcuts and uninstall

4. **Create GitHub Release**
   ```
   Tag: v4.1.0
   Assets: VolMarc_v3-4.1.0-win64.exe
           VolMarc_v3-4.1.0-win32.exe
   ```

5. **Deploy**
   - Update website
   - Send notifications
   - Archive release files

---

## Documentation Files

- `INSTALLER_QUICKSTART.md` - Fast reference
- `INSTALLER_GUIDE.md` - Detailed user guide
- This file - CI/CD technical documentation

---

## Support & Maintenance

For issues or questions:
- **Email**: support@marcslm.com
- **GitHub Issues**: github.com/marcslm/volmarc
- **Website**: https://marcslm.com

---

**Last Updated**: 2024  
**Version**: 4.1.0  
**Status**: Production Ready
