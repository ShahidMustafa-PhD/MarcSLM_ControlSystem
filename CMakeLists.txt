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

# Set CMAKE_PREFIX_PATH for vcpkg (x64)
set(CMAKE_PREFIX_PATH "C:/vcpkg/installed/x64-windows")

# Find Qt6
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/opcserver_lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/scanner_lib)

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
    opcserver_lib/opcserverua.cpp
    opcserver_lib/opcserverua.h
    
    # Scanner Library (merged into DLL)
    scanner_lib/Scanner.cpp
    scanner_lib/Scanner.h
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

# Find and link open62541 (OPC UA library)
# Use vcpkg x64 installed package; ensure the package matches x64 configuration
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # Prefer vcpkg installed path for x64
    set(open62541_DIR "C:/vcpkg/installed/x64-windows/share/open62541" CACHE PATH "Path to open62541Config.cmake")
    find_package(open62541 CONFIG REQUIRED)
    message(STATUS "Found open62541: ${open62541_DIR}")
else()
   # message(FATAL_ERROR "open62541: x86 build not supported. Install open62541 for x86 or build as x64.")
endif()

# Link all dependencies to DLL
target_link_libraries(MarcControl PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    ${RTC5_LIBRARY}
    nlohmann_json::nlohmann_json
    open62541::open62541
)

target_include_directories(MarcControl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/opcserver_lib
    ${CMAKE_CURRENT_SOURCE_DIR}/scanner_lib
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
    Qt5::Core
    Qt5::Widgets
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
# POST-BUILD: COPY QT5 DLLs FROM VCPKG
# ---------------------------
# Copy Qt5 core DLLs
add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/Qt5Core.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Qt5Core.dll to install directory"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/Qt5Gui.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Qt5Gui.dll to install directory"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/Qt5Widgets.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Qt5Widgets.dll to install directory"
)

# Copy Qt5 dependency libraries (ICU, graphics, SSL)
add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icudt74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU data library (icudt74.dll)"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icuin74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU internationalization library (icuin74.dll)"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icuio74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU I/O library (icuio74.dll)"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icutu74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU tools library (icutu74.dll)"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icuuc74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU common library (icuuc74.dll)"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/freetype.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying FreeType font library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/harfbuzz.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying HarfBuzz text shaping library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/harfbuzz-subset.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying HarfBuzz subset library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/jpeg62.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying JPEG library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/libpng16.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying PNG library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/zlib1.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ZLIB compression library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/libcrypto-3-x64.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying OpenSSL crypto library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/libssl-3-x64.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying OpenSSL SSL library"
)

# Copy additional Qt5 dependency libraries
add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/pcre2-16.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying PCRE2 regular expression library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/double-conversion.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying double-conversion library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/bz2.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying bzip2 compression library"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/brotlidec.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Brotli decompression library"
)

# Copy open62541 OPC UA library
add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/open62541.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying open62541 OPC UA library"
)

# Create platforms directory and copy Windows platform plugin
add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${INSTALL_DIR}/platforms"
    COMMENT "Creating platforms directory for Qt plugins"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/plugins/platforms/qwindows.dll"
        "${INSTALL_DIR}/platforms/"
    COMMENT "Copying Qt Windows platform plugin to install directory"
)

add_custom_command(TARGET MarcControl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${INSTALL_DIR}/updates"
    COMMENT "Creating updates directory"
)

# ---------------------------
# POST-BUILD: COPY QT5 DLLs FOR LAUNCHER
# ---------------------------  
add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/Qt5Core.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Qt5Core.dll for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/Qt5Gui.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Qt5Gui.dll for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/Qt5Widgets.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Qt5Widgets.dll for launcher"
)

# Copy Qt5 dependency libraries for launcher
add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icudt74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU data library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icuin74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU internationalization library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icuio74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU I/O library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icutu74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU tools library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/icuuc74.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ICU common library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/freetype.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying FreeType library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/harfbuzz.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying HarfBuzz library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/harfbuzz-subset.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying HarfBuzz subset library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/jpeg62.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying JPEG library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/libpng16.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying PNG library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/zlib1.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying ZLIB library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/libcrypto-3-x64.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying OpenSSL crypto library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/libssl-3-x64.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying OpenSSL SSL library for launcher"
)

# Copy additional Qt5 dependency libraries for launcher
add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/pcre2-16.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying PCRE2 regular expression library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/double-conversion.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying double-conversion library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/bz2.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying bzip2 compression library for launcher"
)

add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/brotlidec.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying Brotli decompression library for launcher"
)

# Copy open62541 OPC UA library for launcher
add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/bin/open62541.dll"
        "${INSTALL_DIR}"
    COMMENT "Copying open62541 OPC UA library for launcher"
)

# Ensure platforms directory exists for launcher too
add_custom_command(TARGET MarcSLM_Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${INSTALL_DIR}/platforms"
    COMMENT "Ensuring platforms directory exists for launcher"
)

message(STATUS "===========================================")
message(STATUS "===========================================")

# Add OPC UA Simulator as separate target (optional)
# The simulator is a standalone executable that must run independently
# It provides the OPC UA server endpoints that the client connects to
add_subdirectory(OPCUASimulator)

message(STATUS "OPCUASimulator added to build")
message(STATUS "To build the simulator, use OPCUASimulator/CMakeLists.txt")