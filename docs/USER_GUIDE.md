# MarcSLM — User Guide

Version: 4.1

This document explains how to build, configure, and use the MarcSLM Machine Control application shipped in this repository. It's focused on the typical operator workflow and explains the two main process modes: Production (slice-file driven, OPC-synchronized) and Test (synthetic layers, isolated).

----

## Table of Contents

- Quick Start
- System Requirements
- Building the Application
- Launching the Application
- Main Window Overview
- Project Management (MARC / JSON)
- Scanner and OPC Initialization
- Running Processes
  - Production Mode (slice-file + JSON + OPC)
  - Test Mode (synthetic layers)
- SVG Export
- Diagnostics & Logs
- Emergency Stop & Safety
- Troubleshooting
- Developer Notes (code layout)

----

## Quick Start

1. Build the project with CMake (Ninja) and your toolchain.
2. Launch the application GUI.
3. Open or create a project and attach a `.marc` file and the configuration `.json` (if using production mode).
4. Initialize OPC (`Initialize OPC` button) and Scanner (`Initialize Scanner` button).
5. Start the production process via `Start Process` (Run menu) or run a `Test SLM Process` for synthetic layers.

----

## System Requirements

- Windows (project contains Windows-specific OPC code)
- C++17 toolchain (MSVC/Clang compatible)
- CMake 3.16+ (project tested with CMake 3.31+)
- Ninja generator (recommended but other generators supported)
- QT 5 (headers in `vcpkg`/installed toolchain)
- If using real hardware: RTC5 scanner card and RTC5DLL, and an OPC DA server available

----

## Building the Application

From the repository root:

```bash
mkdir build && cd build
cmake -G Ninja ..
cmake --build .
```

Notes:
- The CMake config in this project uses C++17 and expects dependencies (Qt and others) available in the configured environment (vcpkg or system).
- Building on Windows with MSVC is the common configuration for this codebase.

----

## Launching the Application

Run the produced executable from the build output directory. The GUI window (`MainWindow`) provides the full operational interface.

----

## Main Window Overview

Key UI areas:
- Left column
  - `Machine Control` buttons: `Test SLM Process` (`InitOPC` button label in UI), `Initialize Scanner`, `Machine Start Up`, `Restart SLM Process`.
  - `Real-Time Status`: readouts for cylinder positions, process state and counters (`sourceCylPos`, `sinkCylPos`, `stacksLeft`, etc.).
  - `System Log`: main text log (`textEdit`).
- Right column
  - Powder Fill and Bottom Layer operations (start powder fill, lay surface, make bottom layers).
  - Scanner control: laser power, speeds, wobble settings and diagnostics.
  - Emergency Control: `EMERGENCY STOP` button.
- Menus
  - `File`, `Edit`, `View`, `Run`, `Project`, `Help` — common actions (open/save/export, start/pause/stop process, attach files, generate SVGs).

----

## Project Management (MARC / JSON)

- Use `Project -> Open Project` or create a new project (`File -> New Project`).
- Attach a `.marc` file (scan slices) via `Project -> Attach Scan Data (.marc)...`.
- Attach the JSON configuration with build styles via `Project -> Attach Configuration (.json)...`.
- The Project Explorer dock shows the project's `MARC` and `JSON` attachments and basic build statistics.

----

## Scanner and OPC Initialization

1. Initialize OPC first (recommended) using `Initialize OPC` (or `Run -> Initialize System`). The OPC controller manages PLC/recoater synchronization.
2. Initialize the Scanner using `Initialize Scanner`.

Notes:
- If OPC is not available, Test Mode can be used without OPC.
- Auto-initialize: when OPC is initialized the UI will attempt to auto-initialize the Scanner.

----

## Running Processes

### Production Mode (slice-file + JSON + OPC)

1. Ensure OPC and Scanner are initialized.
2. Attach a `.marc` file and a `config.json` with build styles.
3. Use `Run -> Start Process` or `Start Process` button to begin.
4. The streaming manager will:
   - Load the JSON in the consumer thread (scanner owner thread).
   - Stream layers from the `.marc` file (producer thread) converting them into `RTCCommandBlock`s with per-segment parameters.
   - Synchronize with OPC for layer creation using a bidirectional handshake: scanner requests layer parameters, PLC prepares the surface, consumer executes scanning, scanner notifies PLC when execution completes.

Status and progress are shown in the log and status bar. The `System Log` shows step messages.

### Test Mode (synthetic layers)

- Use `Test SLM Process` to open a configuration dialog.
- Choose `Layer Thickness` and `Number of Layers`.
- Test mode does not require OPC or a `.marc` file. It generates simple synthetic layers that exercise the scanner control logic.
- Laser is set to pilot (OFF) in test mode for safe hardware testing.

----

## SVG Export

- `View -> Generate SVGs from marc file...` will export 2D SVG images of all layers.
- The UI asks for the `.marc` file (if not already attached to the project) and writes SVGs to the project `svgOutput` folder.
- SVG scale is configurable in the Scanner Control panel (`SVG Scale` control).

----

## Diagnostics & Logs

- The `System Log` (`textEdit`) records process steps, warnings, and errors.
- `Run Scanner Diagnostics` runs hardware checks implemented in the `ScannerController` and updates `scannerStatusDisplay` and `scannerErrorLabel`.
- For low-level scanner logs, review `scanner_lib` code (Scanner class) and `controllers/scannercontroller.*`.

----

## Emergency Stop & Safety

- The `EMERGENCY STOP` button immediately triggers a full stop via `ProcessController::emergencyStop()`.
- In production mode, the bidirectional OPC handshake and laser-disable step ensure the laser is off before mechanical movement.
- Always verify hardware safety covers and powder reservoirs before starting any production run.

----

## Troubleshooting

- "OPC not initialized": Ensure a compatible OPC DA server is running. Initialize OPC via `Initialize OPC` menu/button.
- "Scanner initialization failed": Verify RTC5 card presence and that `RTC5DLL.DLL` and correction files are available.
- If streaming fails with parameter errors, inspect the `config.json` to ensure required `buildStyles` exist and that geometry tags in MARC map to `id`s in JSON.
- If GUI elements are unresponsive, check the console/log output and ensure background threads are not blocked. Use `Restart SLM Process` to restart process monitoring.

----

## Developer Notes (code layout)

- GUI entry point: `launcher/mainwindow.cpp` (UI behavior and button handlers).
- Controllers:
  - `controllers/opccontroller.*` — OPC server and PLC integration.
  - `controllers/scannercontroller.*` — Scanner (RTC5) wrapper operations.
  - `controllers/processcontroller.*` — High-level process state machine.
  - `controllers/scanstreamingmanager.*` — Producer-consumer streaming of `.marc` ? `RTCCommandBlock`.
  - `controllers/slm_worker_manager.*` — Thread ownership for OPC/Scanner workers.
- Scanner implementation: `scanner_lib/Scanner.h` and `scanner_lib/Scanner.cpp`.
- MARC reader and converters: `io/streamingmarcreader.*`, `io/rtccommandblock.h`.

Key public interfaces referenced by the UI:
- `ScanStreamingManager::startProcess(const std::wstring& marcPath, const std::wstring& configJsonPath)` — production streaming start.
- `ScanStreamingManager::startTestProcess(float testLayerThickness, size_t testLayerCount)` — test mode start.
- `ProcessController::startProductionSLMProcess(...)` and `startTestSLMProcess(...)` — orchestrate start of production/test flows.

----

## Contact & Support

- For operational questions about hardware, consult your machine vendor and the RTC5/PLC documentation.
- For code issues, open an issue in the repository including:
  - Steps to reproduce
  - Relevant log excerpts from the `System Log`
  - Config files used (`.marc`, `config.json`)

----

This guide is intended as an operational overview. For further developer-level documentation, consult inline source comments and header files in `controllers/`, `scanner_lib/`, and `io/`.
