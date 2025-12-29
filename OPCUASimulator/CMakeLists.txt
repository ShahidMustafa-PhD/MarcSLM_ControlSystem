# OPC UA Simulator Executable (Standalone)
# Builds independent from main MarcControl DLL

# Enforce x64 build
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  #  message(FATAL_ERROR "OPCUASimulator requires a 64-bit (x64) build. Configure with -A x64.")
endif()

add_executable(OPCUASimulator
    main.cpp
    opcua_sim_server.cpp
    opcua_sim_server.h
)

# C++17 requirement
target_compile_features(OPCUASimulator PRIVATE cxx_std_17)
set(CMAKE_CXX_COMPILER "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" CACHE STRING "" FORCE)
# Find open62541

   set(CMAKE_PREFIX_PATH "C:/vcpkg/installed/x64-windows")
    find_package(open62541 CONFIG REQUIRED)
    message(STATUS "Found open62541: ${open62541_DIR}")


# Link open62541
target_link_libraries(OPCUASimulator PRIVATE
    open62541::open62541
)

# Include directories
target_include_directories(OPCUASimulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${open62541_INCLUDE_DIRS}
)

# Set output directory
set_target_properties(OPCUASimulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../install"
)

# Post-build: Copy open62541 DLL if needed
add_custom_command(TARGET OPCUASimulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/open62541.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/../install"
    COMMENT "Copying open62541 OPC UA library for simulator"
)

message(STATUS "OPCUASimulator configured for x64")
