# ============================================================================
# MarcSLM Control System Version Configuration
# Centralized versioning for CI/CD pipelines
# ============================================================================

# Semantic Versioning (major.minor.patch)
set(MARCSLM_VERSION_MAJOR 4)
set(MARCSLM_VERSION_MINOR 1)
set(MARCSLM_VERSION_PATCH 0)

# Build metadata (for CI/CD)
set(MARCSLM_VERSION_BUILD "")  # Set by CI/CD: e.g., "build.123" or "rc.1"
set(MARCSLM_BUILD_TIMESTAMP "")  # Set by CI/CD: e.g., "2024-01-15T10:30:00Z"

# Construct full version string
set(MARCSLM_VERSION_FULL "${MARCSLM_VERSION_MAJOR}.${MARCSLM_VERSION_MINOR}.${MARCSLM_VERSION_PATCH}")
if(MARCSLM_VERSION_BUILD)
    set(MARCSLM_VERSION_FULL "${MARCSLM_VERSION_FULL}-${MARCSLM_VERSION_BUILD}")
endif()

# Git integration (optional, for CI/CD)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE MARCSLM_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MARCSLM_GIT_HASH)
        set(MARCSLM_VERSION_FULL "${MARCSLM_VERSION_FULL}+git.${MARCSLM_GIT_HASH}")
    endif()
endif()

# Product Information
set(MARCSLM_PRODUCT_NAME "MarcSLM Control System")
set(MARCSLM_COMPANY_NAME "Shahid Mustafa")
set(MARCSLM_PRODUCT_DESCRIPTION "Advanced Selective Laser Melting Machine Control System")

# Export for parent CMakeLists.txt
set(MARCSLM_VERSION_MAJOR ${MARCSLM_VERSION_MAJOR} PARENT_SCOPE)
set(MARCSLM_VERSION_MINOR ${MARCSLM_VERSION_MINOR} PARENT_SCOPE)
set(MARCSLM_VERSION_PATCH ${MARCSLM_VERSION_PATCH} PARENT_SCOPE)
set(MARCSLM_VERSION_FULL ${MARCSLM_VERSION_FULL} PARENT_SCOPE)
set(MARCSLM_PRODUCT_NAME ${MARCSLM_PRODUCT_NAME} PARENT_SCOPE)
set(MARCSLM_COMPANY_NAME ${MARCSLM_COMPANY_NAME} PARENT_SCOPE)
set(MARCSLM_PRODUCT_DESCRIPTION ${MARCSLM_PRODUCT_DESCRIPTION} PARENT_SCOPE)
