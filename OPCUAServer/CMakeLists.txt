# ============================================================================
# OPC UA Production Server for Industrial SLM Control
# ============================================================================
#
# This CMakeLists.txt builds a production-grade OPC UA server that interfaces
# with real PLCs and SLM hardware. It replaces the simulator for deployment.
#
# Features:
# - Industrial-grade error handling and fail-safe logic
# - RAII-based memory management
# - High-frequency data updates without blocking
# - Configurable PLC simulation delays for testing
#
# Build Requirements:
# - CMake 3.16+
# - C++17 compiler
# - open62541 library (via vcpkg or system)
# - x64 architecture
#
# Usage:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target OPCUAServer
#
# ============================================================================

cmake_minimum_required(VERSION 3.16)

# Enforce x64 build for production safety
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "OPCUAServer is designed for 64-bit (x64) builds. 32-bit builds may have issues.")
endif()

# ============================================================================
# Executable Definition
# ============================================================================

add_executable(OPCUAServer
    main.cpp
    slm_opcua_server.cpp
    slm_opcua_server.h
    plc_state.h
    fail_safe_controller.cpp
    fail_safe_controller.h
    codesys_plc_interface.cpp
    codesys_plc_interface.h
)

# C++17 is required for structured bindings, std::optional, and RAII patterns
target_compile_features(OPCUAServer PRIVATE cxx_std_17)

# ============================================================================
# Dependencies
# ============================================================================

# Find open62541 (prefer vcpkg toolchain discovery)
find_package(open62541 CONFIG QUIET)
if(NOT open62541_FOUND)
    message(WARNING 
        "open62541 package not found. Skipping OPCUAServer target.\n"
        "To build, provide one of:\n"
        "  - -Dopen62541_DIR=/path/to/open62541/cmake\n"
        "  - Use vcpkg toolchain: -DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake"
    )
    return()
endif()
message(STATUS "Found open62541 for OPCUAServer")

# Find libmodbus for CoDeSys PLC communication
find_package(unofficial-libmodbus CONFIG QUIET)
if(NOT unofficial-libmodbus_FOUND)
    message(WARNING 
        "libmodbus package not found. CoDeSys PLC interface will be disabled.\n"
        "To enable:\n"
        "  - Install via vcpkg: vcpkg install libmodbus:x64-windows\n"
        "  - Or provide: -Dunofficial-libmodbus_DIR=/path/to/libmodbus/cmake"
    )
    # Still build OPCUAServer without PLC interface
    target_compile_definitions(OPCUAServer PRIVATE NO_PLC_INTERFACE)
else()
    message(STATUS "Found libmodbus for CoDeSys PLC interface")
    target_link_libraries(OPCUAServer PRIVATE
        unofficial::libmodbus::modbus
    )
    target_compile_definitions(OPCUAServer PRIVATE HAS_PLC_INTERFACE)
endif()

# Link against open62541
target_link_libraries(OPCUAServer PRIVATE
    open62541::open62541
)

# Platform-specific networking libraries
if(WIN32)
    target_link_libraries(OPCUAServer PRIVATE ws2_32 iphlpapi)
endif()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(OPCUAServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Compiler Warnings & Definitions
# ============================================================================

if(MSVC)
    target_compile_options(OPCUAServer PRIVATE
        /W4         # High warning level
        /WX-        # Treat warnings as errors (disabled for open62541 headers)
        /wd4100     # Unreferenced formal parameter (common in callbacks)
        /wd4244     # Conversion warnings from open62541
    )
    target_compile_definitions(OPCUAServer PRIVATE
        _CRT_SECURE_NO_WARNINGS     # Disable MSVC secure CRT warnings
        NOMINMAX                     # Prevent Windows.h from defining min/max macros
        WIN32_LEAN_AND_MEAN          # Exclude rarely-used Windows headers
    )
else()
    target_compile_options(OPCUAServer PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter       # Common in callbacks
    )
endif()

# ============================================================================
# Output Configuration
# ============================================================================

# Install to project root install/ directory (same as main application)
set_target_properties(OPCUAServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../install"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/../install"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../install"
)

# ============================================================================
# Post-Build: Copy Required DLLs (Windows with vcpkg)
# ============================================================================

if(WIN32 AND DEFINED CMAKE_TOOLCHAIN_FILE)
    get_filename_component(_vcpkg_root "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_vcpkg_root "${_vcpkg_root}" DIRECTORY)
    
    # Try to find open62541.dll
    set(_open62541_dll "${_vcpkg_root}/installed/x64-windows/bin/open62541.dll")
    if(EXISTS "${_open62541_dll}")
        add_custom_command(TARGET OPCUAServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_open62541_dll}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../install"
            COMMENT "Copying open62541.dll for OPCUAServer"
        )
    endif()
endif()

# ============================================================================
# Installation Rules (for packaging)
# ============================================================================

install(TARGETS OPCUAServer
    RUNTIME DESTINATION bin
    COMPONENT server
)

# ============================================================================
# Status Message
# ============================================================================

message(STATUS "OPCUAServer configured (Production SLM OPC UA Server)")
message(STATUS "  - Output directory: ${CMAKE_CURRENT_SOURCE_DIR}/../install")
message(STATUS "  - C++ Standard: C++17")
message(STATUS "  - Features: Fail-safe logic, RAII, high-frequency updates")

