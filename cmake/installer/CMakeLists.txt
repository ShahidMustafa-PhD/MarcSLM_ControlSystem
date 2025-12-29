# ============================================================================
# MarcSLM Control System - Installer Configuration Module
# Industrial-grade CI/CD ready packaging
# ============================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------
# Component Registration
# ---------------------------
# Define install components for modular packaging
set(MARCSLM_INSTALL_COMPONENTS
    runtime       # Main executable
    libraries     # DLLs and runtime libraries
    plugins       # Qt plugins and platform support
    data          # Data files (RTC5Dat.dat, configs)
    documentation # README, LICENSE, manuals
)

# ---------------------------
# Installation Paths
# ---------------------------
# Follow FHS-like conventions for Windows installations
if(WIN32)
    set(MARCSLM_INSTALL_BIN_DIR "bin")
    set(MARCSLM_INSTALL_LIB_DIR "lib")
    set(MARCSLM_INSTALL_PLUGIN_DIR "bin/plugins")
    set(MARCSLM_INSTALL_DATA_DIR "data")
    set(MARCSLM_INSTALL_DOC_DIR "doc")
    set(MARCSLM_INSTALL_CONFIG_DIR "config")
else()
    set(MARCSLM_INSTALL_BIN_DIR "bin")
    set(MARCSLM_INSTALL_LIB_DIR "lib")
    set(MARCSLM_INSTALL_PLUGIN_DIR "lib/plugins")
    set(MARCSLM_INSTALL_DATA_DIR "share/marcslm/data")
    set(MARCSLM_INSTALL_DOC_DIR "share/doc/marcslm")
    set(MARCSLM_INSTALL_CONFIG_DIR "etc/marcslm")
endif()

# ---------------------------
# Main Executable Installation
# ---------------------------
install(TARGETS MarcSLM_ControlSystem
    RUNTIME DESTINATION ${MARCSLM_INSTALL_BIN_DIR}
    COMPONENT runtime
)

# ---------------------------
# Critical Libraries Installation
# ---------------------------
# RTC5 DLL and data files (scanner hardware support)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(RTC5_DLL "${RTC5_LIB_DIR}/RTC5DLLx64.dll")
else()
    set(RTC5_DLL "${RTC5_LIB_DIR}/RTC5DLL.dll")
endif()

install(FILES "${RTC5_DLL}"
    DESTINATION ${MARCSLM_INSTALL_BIN_DIR}
    COMPONENT libraries
)

install(FILES "${RTC5_LIB_DIR}/RTC5Dat.dat"
    DESTINATION ${MARCSLM_INSTALL_DATA_DIR}
    COMPONENT data
)

# Qt Core Libraries
install(FILES
    "${QT_LIB_DIR}/bin/Qt5Core.dll"
    "${QT_LIB_DIR}/bin/Qt5Gui.dll"
    "${QT_LIB_DIR}/bin/Qt5Widgets.dll"
    DESTINATION ${MARCSLM_INSTALL_BIN_DIR}
    COMPONENT libraries
    OPTIONAL
)

# Qt Platform Plugin (required for GUI)
install(FILES "${QT_PLUGINS_DIR}/platforms/qwindows.dll"
    DESTINATION ${MARCSLM_INSTALL_PLUGIN_DIR}/platforms
    COMPONENT plugins
    OPTIONAL
)

# Qt Image Format Plugins
install(DIRECTORY "${QT_PLUGINS_DIR}/imageformats/"
    DESTINATION ${MARCSLM_INSTALL_PLUGIN_DIR}/imageformats
    COMPONENT plugins
    OPTIONAL
)

# Qt Icon Engine Plugins
install(DIRECTORY "${QT_PLUGINS_DIR}/iconengines/"
    DESTINATION ${MARCSLM_INSTALL_PLUGIN_DIR}/iconengines
    COMPONENT plugins
    OPTIONAL
)

# ---------------------------
# Documentation Installation
# ---------------------------
install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE.txt"
    DESTINATION ${MARCSLM_INSTALL_DOC_DIR}
    COMPONENT documentation
)

install(FILES
    "${CMAKE_SOURCE_DIR}/README.md"
    DESTINATION ${MARCSLM_INSTALL_DOC_DIR}
    COMPONENT documentation
    OPTIONAL
)

# ---------------------------
# CPACK Configuration
# ---------------------------
include(InstallRequiredSystemLibraries)

# Package metadata
set(CPACK_PACKAGE_NAME "MarcSLM-ControlSystem")
set(CPACK_PACKAGE_VENDOR "Shahid Mustafa")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MarcSLM Control System - Advanced Selective Laser Melting")
set(CPACK_PACKAGE_DESCRIPTION "MarcSLM Control System is a comprehensive machine control system for selective laser melting operations with real-time process monitoring, OPC DA server integration, and RTC5 scanner control. Developed by Shahid Mustafa.")

set(CPACK_PACKAGE_VERSION_MAJOR ${MARCSLM_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${MARCSLM_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${MARCSLM_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

# Install directory
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MarcSLM-ControlSystem")
set(CPACK_PACKAGE_EXECUTABLES "MarcSLM_ControlSystem" "MarcSLM Control System")

# License
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.txt")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
endif()

# Architecture detection for package naming
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CPACK_SYSTEM_NAME "win64")
else()
    set(CPACK_SYSTEM_NAME "win32")
endif()

# Package filename format: MarcSLM-ControlSystem-4.1.0-win64.exe
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")

# ---------------------------
# NSIS-Specific Configuration (Windows Installer)
# ---------------------------
set(CPACK_GENERATOR "NSIS")

# Display settings
set(CPACK_NSIS_DISPLAY_NAME "MarcSLM Control System v${CPACK_PACKAGE_VERSION}")
set(CPACK_NSIS_PACKAGE_NAME "MarcSLM-ControlSystem")

# Branding (optional custom images)
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/installer.ico")
    set(CPACK_NSIS_INSTALLED_IMAGE_ICO_INSTALL "${CMAKE_SOURCE_DIR}/assets/installer.ico")
    set(CPACK_NSIS_INSTALLED_IMAGE_ICO_UNINSTALL "${CMAKE_SOURCE_DIR}/assets/uninstaller.ico")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/assets/header.bmp")
    set(CPACK_NSIS_MUI_HEADERIMAGE "${CMAKE_SOURCE_DIR}/assets/header.bmp")
    set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/assets/welcome.bmp")
    set(CPACK_NSIS_MUI_UNWELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/assets/welcome.bmp")
endif()

# URLs and contact
set(CPACK_NSIS_CONTACT "contact@marcslm.com")
set(CPACK_NSIS_HELP_LINK "https://marcslm.com")
set(CPACK_NSIS_URL_INFO_ABOUT "https://marcslm.com/about")
set(CPACK_NSIS_URL_UPDATE_INFO "https://marcslm.com/download")

# Installer behavior
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")

# Shortcuts creation
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\$STARTMENU_FOLDER\\MarcSLM Control System.lnk' '$INSTDIR\\bin\\MarcSLM_ControlSystem.exe'"
    "CreateShortCut '$DESKTOP\\MarcSLM Control System.lnk' '$INSTDIR\\bin\\MarcSLM_ControlSystem.exe'"
    "CreateShortCut '$SMPROGRAMS\\$STARTMENU_FOLDER\\Documentation.lnk' '$INSTDIR\\doc'"
)

set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\$STARTMENU_FOLDER\\MarcSLM Control System.lnk'"
    "Delete '$SMPROGRAMS\\$STARTMENU_FOLDER\\Documentation.lnk'"
    "Delete '$DESKTOP\\MarcSLM Control System.lnk'"
)

# Start menu folder
set(CPACK_NSIS_MENU_LINKS
    "bin/MarcSLM_ControlSystem.exe" "MarcSLM Control System"
    "https://marcslm.com" "MarcSLM Website"
)

# Component descriptions
set(CPACK_NSIS_COMPONENT_INSTALL ON)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Application (required)")
set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries and DLLs (required)")
set(CPACK_COMPONENT_PLUGINS_DISPLAY_NAME "Qt Plugins (required)")
set(CPACK_COMPONENT_DATA_DISPLAY_NAME "Data Files")
set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")

set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)
set(CPACK_COMPONENT_LIBRARIES_REQUIRED ON)
set(CPACK_COMPONENT_PLUGINS_REQUIRED ON)

# ---------------------------
# CPack Finalization
# ---------------------------
include(CPack)

# ---------------------------
# Status Output
# ---------------------------
message(STATUS "")
message(STATUS "============================================================")
message(STATUS "Installer Module Configuration (Industrial CI/CD Ready)")
message(STATUS "============================================================")
message(STATUS "Package Name:        ${CPACK_PACKAGE_NAME}")
message(STATUS "Product:             ${MARCSLM_PRODUCT_NAME}")
message(STATUS "Company:             ${MARCSLM_COMPANY_NAME}")
message(STATUS "Version:             ${CPACK_PACKAGE_VERSION}")
message(STATUS "System:              ${CPACK_SYSTEM_NAME}")
message(STATUS "Generator:           ${CPACK_GENERATOR}")
message(STATUS "Install Dir:         ${CPACK_PACKAGE_INSTALL_DIRECTORY}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  • runtime          -> ${MARCSLM_INSTALL_BIN_DIR}/MarcSLM_ControlSystem.exe")
message(STATUS "  • libraries        -> ${MARCSLM_INSTALL_BIN_DIR}/*.dll")
message(STATUS "  • plugins          -> ${MARCSLM_INSTALL_PLUGIN_DIR}/")
message(STATUS "  • data             -> ${MARCSLM_INSTALL_DATA_DIR}/")
message(STATUS "  • documentation    -> ${MARCSLM_INSTALL_DOC_DIR}/")
message(STATUS "")
message(STATUS "Output File:         ${CPACK_PACKAGE_FILE_NAME}.exe")
message(STATUS "============================================================")
message(STATUS "")
