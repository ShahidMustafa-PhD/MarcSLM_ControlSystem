# OPC UA Simulator Executable (Standalone)
# Builds independent from main MarcControl DLL

# Enforce x64 build
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  # message(FATAL_ERROR "OPCUASimulator requires a 64-bit (x64) build. Configure with -A x64.")
endif()

add_executable(OPCUASimulator
    main.cpp
    opcua_sim_server.cpp
    opcua_sim_server.h
)

# C++17 requirement
target_compile_features(OPCUASimulator PRIVATE cxx_std_17)

# Find open62541
# - Prefer discovery from the parent toolchain (e.g., vcpkg toolchain file)
# - Do not hard-code compiler paths or local vcpkg locations
find_package(open62541 CONFIG QUIET)
if(NOT open62541_FOUND)
    message(WARNING "open62541 package not found; skipping OPCUASimulator target. Provide -Dopen62541_DIR=... or use a toolchain (e.g., vcpkg) that installs open62541.")
    return()
endif()
message(STATUS "Found open62541")

# Link open62541
target_link_libraries(OPCUASimulator PRIVATE
    open62541::open62541
)

# Include directories
target_include_directories(OPCUASimulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set output directory
set_target_properties(OPCUASimulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../install"
)

# Post-build: Copy open62541 DLL (only when using a vcpkg-style layout)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}" AND EXISTS "${CMAKE_TOOLCHAIN_FILE}/../installed")
    get_filename_component(_vcpkg_root "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_vcpkg_root "${_vcpkg_root}" DIRECTORY)
    set(_open62541_dll "${_vcpkg_root}/installed/x64-windows/bin/open62541.dll")
    if(EXISTS "${_open62541_dll}")
        add_custom_command(TARGET OPCUASimulator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_open62541_dll}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../install"
            COMMENT "Copying open62541 OPC UA library for simulator"
        )
    endif()
endif()

message(STATUS "OPCUASimulator configured")
