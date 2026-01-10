cmake_minimum_required(VERSION 3.16)
project(MarcSLM_ControlSystem LANGUAGES CXX C)

# Enforce x64 build - project requires x64 vcpkg packages
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    #message(FATAL_ERROR "This project requires a 64-bit (x64) build.\n\nTo build for x64, use:\n  cmake -S . -B build-x64 -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake\n  cmake --build build-x64 --config Release\n\nOr use the x64-release preset if your environment supports it.\n\n32-bit builds are not supported.")
endif()

# C++ Version
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_definitions(-DQT_NO_CONCEPTS -D_MBCS -UUNICODE -U_UNICODE)

# vcpkg integration:
# Prefer discovery from the CMAKE_TOOLCHAIN_FILE provided by the build environment.
# Do not hard-code a machine-specific vcpkg root here.
# (Visual Studio CMake integration and vcpkg toolchain will populate CMAKE_PREFIX_PATH automatically.)
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/vcpkg/installed/x64-windows")
endif()

 find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui)
 set(_qt_targets Qt5::Core Qt5::Gui Qt5::Widgets)


# ---------------------------
# VERSION CONFIGURATION
# ---------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake)
message(STATUS "${MARCSLM_PRODUCT_NAME} Version: ${MARCSLM_VERSION_FULL}")
message(STATUS "Product: ${MARCSLM_PRODUCT_NAME}")
message(STATUS "Company: ${MARCSLM_COMPANY_NAME}")

# ---------------------------
# RTC5 LIBRARY CONFIGURATION
# ---------------------------
set(RTC5_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/RTC5 Files")
set(RTC5_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/RTC5 Files")

# Detect architecture for correct library
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64-bit build
    set(RTC5_LIBRARY "${RTC5_LIB_DIR}/RTC5DLLx64.lib")
    message(STATUS "Using 64-bit RTC5 library: ${RTC5_LIBRARY}")
else()
    # 32-bit build (not supported by this project)
    set(RTC5_LIBRARY "${RTC5_LIB_DIR}/RTC5DLL.lib")
    message(STATUS "Using 32-bit RTC5 library: ${RTC5_LIBRARY}")
endif()

# Verify library exists
if(NOT EXISTS "${RTC5_LIBRARY}")
    message(FATAL_ERROR "RTC5 library not found at: ${RTC5_LIBRARY}")
endif()

# ---------------------------
# SOURCE FILES
# ---------------------------
# OPC DA library removed - migrated to OPC UA

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/opcserver)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/scanner)

# Add controllers directory to include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/controllers)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ui)
include_directories(${RTC5_INCLUDE_DIR})

if(CMAKE_BUILD_TYPE)
    string(TOLOWER ${CMAKE_BUILD_TYPE} BUILD_TYPE_LOWER)
else()
    set(BUILD_TYPE_LOWER "release")
endif()
set(INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/install")
# ---------------------------
# BUILD MARCCONTROL DLL (All-in-One)
# ---------------------------
add_library(MarcControl SHARED
    # DLL Entry Point
    launcher/MarcControlEntry.cpp
    launcher/MarcControl.h
    
    # Core UI
    launcher/mainwindow.cpp
    launcher/mainwindow.h
    launcher/ProjectManager.cpp
    launcher/ProjectManager.h
    
    # Controllers
    controllers/opccontroller.cpp
    controllers/opccontroller.h
    controllers/scannercontroller.cpp
    controllers/scannercontroller.h
    controllers/processcontroller.cpp
    controllers/processcontroller.h
    controllers/scanstreamingmanager.cpp
    controllers/scanstreamingmanager.h
    controllers/slm_worker_manager.cpp
    controllers/slm_worker_manager.h
    
    # I/O
    io/readSlices.cpp
    io/readSlices.h
    io/writeSVG.cpp
    io/writeSVG.h
    io/file2RTC.cpp
    io/file2RTC.h
    io/streamingmarcreader.cpp
    io/streamingmarcreader.h
    io/buildstyle.cpp
    io/buildstyle.h
    io/rtccommandblock.cpp
    io/rtccommandblock.h
    
    # OPC UA Library (merged into DLL) - replaces OPC DA
    opcserver/opcserverua.cpp
    opcserver/opcserverua.h
    
    # Scanner Library (merged into DLL)
    scanner/Scanner.cpp
    scanner/Scanner.h
    ${RTC5_INCLUDE_DIR}/RTC5expl.c
)

# Define export macro for DLL
target_compile_definitions(MarcControl PRIVATE 
    MARCCONTROL_EXPORTS
    QT_NO_CONCEPTS
    _MBCS
)

# Set DLL properties
set_target_properties(MarcControl PROPERTIES
    OUTPUT_NAME "MarcControl"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${INSTALL_DIR}"
)

# Enable Qt MOC for DLL
set_target_properties(MarcControl PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Find and link nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)

# Find open62541 (OPC UA library)
# Prefer discovery from the parent toolchain (e.g., vcpkg toolchain).
find_package(open62541 CONFIG QUIET)
if(open62541_FOUND)
    message(STATUS "Found open62541")
else()
    message(WARNING "open62541 package not found; MarcControl will be built without embedded OPC UA server support.")
endif()

# Link all dependencies to DLL
target_link_libraries(MarcControl PRIVATE
    ${_qt_targets}
    ${RTC5_LIBRARY}
    nlohmann_json::nlohmann_json
)

if(open62541_FOUND)
    target_link_libraries(MarcControl PRIVATE open62541::open62541)
    target_compile_definitions(MarcControl PRIVATE MARCSLM_HAS_OPEN62541=1)
else()
    target_compile_definitions(MarcControl PRIVATE MARCSLM_HAS_OPEN62541=0)
endif()

target_include_directories(MarcControl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/opcserver
    ${CMAKE_CURRENT_SOURCE_DIR}/scanner
    ${CMAKE_CURRENT_SOURCE_DIR}/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/launcher
    ${RTC5_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# MSVC-specific linker flags
if(MSVC)
    target_link_options(MarcControl PRIVATE 
        /ignore:4006
        /ignore:4217
    )
endif()

# ---------------------------
# BUILD MINIMAL LAUNCHER
# ---------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(MarcSLM_Launcher launcher/main.cpp)
else()
    add_executable(MarcSLM_Launcher WIN32 launcher/main.cpp)
endif()

target_link_libraries(MarcSLM_Launcher PRIVATE
    ${_qt_targets}
)

target_include_directories(MarcSLM_Launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(MarcSLM_Launcher PROPERTIES
    OUTPUT_NAME "MarcSLM_Launcher"
    RUNTIME_OUTPUT_DIRECTORY "${INSTALL_DIR}"
)

# ---------------------------
# POST-BUILD: COPY RTC5 DLL
# ---------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(RTC5_DLL "${RTC5_LIB_DIR}/RTC5DLLx64.dll")
else()
    set(RTC5_DLL "${RTC5_LIB_DIR}/RTC5DLL.dll")
endif()

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RTC5_DLL}"
        "${INSTALL_DIR}"
    COMMENT "Copying RTC5 DLL to install directory"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RTC5_LIB_DIR}/RTC5Dat.dat"
        "${INSTALL_DIR}"
    COMMENT "Copying RTC5 data files to install directory"
)

# ---------------------------
# POST-BUILD: COPY QT DLLs (optional convenience)
# ---------------------------
# The existing configuration hard-codes a vcpkg path. That breaks when vcpkg is installed elsewhere.
# If you still want post-build copying, prefer deploying via windeployqt or CMake install rules.

# Copy open62541 OPC UA library (only if found)
if(open62541_FOUND)
    add_custom_command(TARGET MarcControl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:open62541::open62541>"
            "${INSTALL_DIR}"
        COMMENT "Copying open62541 OPC UA library"
    )
endif()

# ---------------------------
# POST-BUILD: COPY QT DLLs FOR LAUNCHER (optional convenience)
# ---------------------------
# (See note above about avoiding hard-coded paths.)

# ---------------------------
# OPTIONAL: OPC UA Simulator
# ---------------------------
add_subdirectory(OPCUASimulator)

message(STATUS "OPCUASimulator added to build")
message(STATUS "To build the simulator, use OPCUASimulator/CMakeLists.txt")