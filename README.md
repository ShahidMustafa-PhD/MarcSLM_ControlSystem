# MarcSLM Control System

[![CMake](https://img.shields.io/badge/build-CMake-informational)](#build--install)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)](#prerequisites)
[![Qt](https://img.shields.io/badge/Qt-5-green)](#prerequisites)
[![OPC%20UA](https://img.shields.io/badge/OPC%20UA-open62541-informational)](https://open62541.org/)

Industrial machine-control software for Selective Laser Melting (SLM) that orchestrates RTC5 scan execution, real-time layer streaming, and PLC integration via OPC UA.

> **Operational documentation**: see `docs/USER_GUIDE.md`.

---

## Table of Contents

- [Overview / Motivation](#overview--motivation)
- [Features](#features)
- [Supported Platforms](#supported-platforms)
- [Build / Install](#build--install)
  - [Prerequisites](#prerequisites)
  - [Dependencies (vcpkg)](#dependencies-vcpkg)
  - [Build Instructions](#build-instructions)
  - [Packaging (Optional)](#packaging-optional)
  - [Docker / CI Notes (Optional)](#docker--ci-notes-optional)
- [Usage](#usage)
  - [Quick Start (GUI)](#quick-start-gui)
  - [Production Mode vs Test Mode](#production-mode-vs-test-mode)
  - [OPC UA Simulator](#opc-ua-simulator)
  - [Runtime Layout](#runtime-layout)
- [Architecture](#architecture)
  - [High-Level Dataflow](#high-level-dataflow)
  - [Key Components](#key-components)
  - [Extensibility Points](#extensibility-points)
- [Code Structure](#code-structure)
- [Testing](#testing)
- [CI/CD](#cicd)
- [Contributing](#contributing)
- [License](#license)
- [Acknowledgements / References](#acknowledgements--references)
- [Known Issues / Limitations](#known-issues--limitations)
- [Roadmap](#roadmap)

---

## Overview / Motivation

Selective Laser Melting control software must coordinate multiple real-time subsystems—laser/scanner motion control, layer-by-layer toolpath streaming, and machine automation (recoater, powder handling, and safety interlocks). This project exists to provide an integrated **machine control layer** that:

- Streams layer geometry (`.marc`) into scan command blocks suitable for RTC5-class controllers.
- Applies build-style parameters from JSON (power/speed/wobble/hatch settings).
- Synchronizes scan execution with a PLC using **OPC UA** handshaking.
- Provides an operator-facing **Qt GUI** for initialization, process execution, diagnostics, logging, and emergency stop.

**Target users**

- Controls/software engineers integrating an SLM machine stack.
- Manufacturing/automation teams validating process sequences and PLC handshakes.
- Researchers prototyping scan strategies using real scanner hardware or a simulator.

**Industrial / scientific significance**

- Deterministic coordination of scan execution with machine motion reduces layer defects and improves repeatability.
- A clean separation between *geometry streaming* and *execution* enables higher-throughput path pipelines and consistent parameter application.
- OPC UA integration supports modern automation interoperability and testability.

---

## Features

### Key capabilities

- **Qt 5 GUI** for operator workflows, project management, and diagnostics.
- **Production mode**: stream `.marc` slice data + JSON build styles with PLC synchronization.
- **Test mode**: generate synthetic layers without requiring a `.marc` file or PLC.
- **RTC5 integration** (vendor DLL + correction data) for scanner control.
- **OPC UA integration** using `open62541`.
- **SVG export** of layers for inspection and traceability.

### Modules / components overview

| Component | Purpose |
|---|---|
| `launcher/` | GUI entry points and `MainWindow` / project management. |
| `controllers/` | High-level orchestration (process, scanner, OPC UA, streaming, worker/thread ownership). |
| `io/` | `.marc` streaming reader, JSON build-style parsing (nlohmann/json), RTC command block generation, SVG writer. |
| `scanner/` | RTC5 scanner wrapper (`Scanner`) and device-level operations. |
| `opcserver/` | OPC UA integration implementation. |
| `OPCUASimulator/` | Standalone simulator target to emulate an OPC UA endpoint for integration testing. |

---

## Supported Platforms

- **Windows (primary / expected)**: MSVC + Qt5 + vendor RTC5 runtime.
- **Linux/macOS**: not currently first-class; significant work may be required due to
  - vendor RTC5 library availability,
  - Windows-specific toolchain assumptions in `CMakeLists.txt` (e.g., `C:/vcpkg/...`).

---

## Build / Install

### Prerequisites

- **CMake**: 3.16+
- **Compiler**:
  - Windows: MSVC (x64)
  - Linux/macOS: Clang/GCC (C++17) *only if dependencies are available and paths are adjusted*
- **Qt**: Qt 5 (`Core`, `Gui`, `Widgets`)
- **vcpkg** (recommended): used for dependency acquisition
- **Dependencies**:
  - `nlohmann_json`
  - `open62541`

Hardware builds additionally require vendor RTC5 files located in `RTC5 Files/`:

- `RTC5DLLx64.lib` / `RTC5DLLx64.dll`
- `RTC5Dat.dat`
- `RTC5expl.c` and associated headers

### Dependencies (vcpkg)

The current CMake configuration expects dependencies in a `vcpkg` x64 installation (defaults reference `C:/vcpkg/installed/x64-windows`).

Example (PowerShell) dependency install:

```powershell
vcpkg install qt5-base:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install open62541:x64-windows
```

If your `vcpkg` location differs, update `CMAKE_PREFIX_PATH` and (if used) `open62541_DIR` in the top-level `CMakeLists.txt`.

### Build Instructions

#### Windows (MSVC, x64)

```powershell
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Artifacts are staged to the repository-local `install/` directory (see `RUNTIME_OUTPUT_DIRECTORY` in `CMakeLists.txt`).

Run:

```powershell
.\install\MarcSLM_Launcher.exe
```

#### Linux (experimental)

Linux builds require:

- A Qt5 development environment discoverable by CMake (`Qt5::Core`, `Qt5::Widgets`).
- `open62541` and `nlohmann_json` discoverable by CMake.
- Replacing Windows-specific paths and the RTC5 vendor dependency.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

#### macOS (experimental)

Similar constraints as Linux apply. You will likely need to set `CMAKE_PREFIX_PATH` to your Qt installation and remove/replace RTC5-specific linkage.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Packaging (Optional)

Installer/packaging scaffolding exists under `cmake/installer/` (CPack/NSIS oriented). A typical packaging workflow is:

```powershell
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
cpack --config build/CPackConfig.cmake
```

### Docker / CI Notes (Optional)

No Dockerfile is currently provided.

For CI on Windows runners:

- Install dependencies via `vcpkg` (cache `installed/`).
- Provide RTC5 vendor binaries via secure artifacts (do not commit redistributables unless permitted).
- Build with CMake and publish `install/` as an artifact.

---

## Usage

### Quick Start (GUI)

1. Build the repository (see [Build Instructions](#build-instructions)).
2. Launch the GUI:

   ```powershell
   .\install\MarcSLM_Launcher.exe
   ```

3. Create/open a project.
4. Attach inputs:
   - `.marc` scan data via the `Project` menu.
   - JSON configuration (build styles) via the `Project` menu.
5. Initialize subsystems:
   - `Initialize OPC`
   - `Initialize Scanner`
6. Start processing:
   - `Run -> Start Process` for production mode, or
   - `Test SLM Process` for synthetic layer execution.

For detailed UI workflow and troubleshooting, refer to `docs/USER_GUIDE.md`.

### Production Mode vs Test Mode

| Mode | Purpose | Inputs | PLC/OPC UA | Typical use |
|---|---|---|---|---|
| Production | Real layer streaming and synchronization | `.marc` + JSON | Required (recommended) | Manufacturing runs, full integration tests |
| Test | Synthetic layers for scanner validation | GUI parameters | Not required | Bench tests, safe validation, diagnostics |

### OPC UA Simulator

A standalone simulator target `OPCUASimulator` is included for development.

Build (from repo root):

```powershell
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --target OPCUASimulator
```

Run:

```powershell
.\install\OPCUASimulator.exe
```

### Runtime Layout

After a successful build, `install/` is used as the runtime staging directory:

- `install/MarcControl.dll` (main shared library)
- `install/MarcSLM_Launcher.exe` (GUI launcher)
- `install/OPCUASimulator.exe` (optional)
- `install/RTC5DLLx64.dll`, `install/RTC5Dat.dat` (hardware runtime)
- `install/platforms/qwindows.dll` (Qt platform plugin)

---

## Architecture

### High-Level Dataflow

```text
          +--------------------+           +----------------------+
          | Qt GUI             |           | PLC / Automation      |
          | `launcher/`        |           | OPC UA endpoint       |
          +---------+----------+           +----------+-----------+
                    |                                 ^
                    v                                 |
          +--------------------+        handshake      |
          | Process controller |<----------------------+
          | `controllers/`     |
          +---------+----------+
                    |
                    v
          +--------------------+      +-------------------+
          | Scan streaming      |----->| RTC command blocks |
          | `controllers/`      |      | `io/`             |
          +---------+----------+       +-------------------+
                    |
         stream .marc| + apply JSON
                    v
          +--------------------+      +-------------------+
          | Scanner controller  |----->| RTC5 DLL / hardware |
          | `controllers/`      |      | `scanner/`         |
          +--------------------+      +-------------------+
```

### Key Components

- `controllers/processcontroller.*`
  - Owns the high-level process state machine (start/pause/stop, emergency stop).
- `controllers/scanstreamingmanager.*`
  - Producer/consumer streaming: `.marc` reader → command blocks → scanner owner thread.
- `controllers/opccontroller.*`
  - OPC UA integration for synchronization and state exchange.
- `controllers/scannercontroller.*` + `scanner/Scanner.*`
  - Initialization, diagnostics, and execution against RTC5 runtime.
- `io/streamingmarcreader.*`
  - Slice streaming from `.marc`.
- `io/buildstyle.*`
  - Build-style parsing and mapping.

### Extensibility Points

- Extend JSON schema parsing in `io/buildstyle.*` and propagate into `io/rtccommandblock.*`.
- Add scan-strategy conversions in the streaming pipeline (`controllers/scanstreamingmanager.*` + `io/`).
- Encapsulate PLC handshake changes in `controllers/opccontroller.*` while keeping `controllers/processcontroller.*` stable.

---

## Code Structure

This repository is organized by function:

| Path | Purpose |
|---|---|
| `launcher/` | Qt GUI entry points (`MainWindow`), project management, DLL entry. |
| `controllers/` | Control-layer orchestration and multithreading (scanner/OPC/process/streaming). |
| `io/` | File formats (`.marc`), JSON config, RTC command generation, SVG export. |
| `scanner/` | Scanner abstraction wrapping the RTC5 library. |
| `opcserver/` | OPC UA logic and server/client glue. |
| `OPCUASimulator/` | Standalone simulator executable. |
| `cmake/` | Versioning and packaging modules. |
| `docs/` | Operator and developer documentation. |
| `install/` | Local staging folder for runtime artifacts (generated). |

---

## Testing

No dedicated `tests/` target is currently defined.

Recommended engineering practice for expanding test coverage:

- Unit tests for JSON/build-style parsing and `.marc` conversion logic.
- Integration tests using `OPCUASimulator` to validate handshake behavior.

Suggested frameworks:

- GoogleTest (via `vcpkg`), or
- Qt Test (if aligning with Qt tooling).

---

## CI/CD

No pipeline files are included by default.

Suggested baseline:

- GitHub Actions (Windows runner)
  - dependency restore via `vcpkg`
  - CMake configure/build
  - `ctest` execution when tests exist
  - artifact publication of `install/`
- Release automation
  - CPack/NSIS installer generation
  - code signing (if required)

---

## Contributing

1. Fork the repository.
2. Create a topic branch from `main` (`feature/<topic>` or `fix/<topic>`).
3. Keep changes focused and submit a pull request.

Pull requests should include:

- A clear problem statement and rationale.
- Build instructions or logs (toolchain and configuration).
- Tests or validation steps where applicable.

Coding standards:

- C++17.
- Deterministic ownership and explicit thread boundaries.
- Avoid UI-thread blocking work; keep hardware I/O isolated.
- Keep dependencies explicit in `CMakeLists.txt`.

---

## License

See `LICENSE.txt`.

- **SPDX identifier**: `LicenseRef-Proprietary`
- Summary: proprietary end-user license; redistribution/modification require separate authorization.

---

## Acknowledgements / References

- Qt: https://www.qt.io/
- open62541 (OPC UA): https://open62541.org/
- nlohmann/json: https://github.com/nlohmann/json
- RTC5 interface: vendor-provided runtime and headers in `RTC5 Files/`

---

## Known Issues / Limitations

- Build scripts include **hard-coded Windows paths** (e.g., `C:/vcpkg/...`) and may require adjustment.
- Hardware builds depend on vendor libraries that may not be redistributable.
- No first-class unit test targets are currently defined.

---

## Roadmap

- Improve portability (remove hard-coded dependency paths; add CMake presets).
- Add `ctest`-driven unit tests for `io/` parsing and conversion.
- Add headless regression mode for streaming pipeline validation.
- Add CI packaging and release automation.
