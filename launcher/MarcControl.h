// ============================================================================
// MarcControl DLL Header - Exported Interface
// ============================================================================

#ifndef MARCCONTROL_H
#define MARCCONTROL_H

#ifdef MARCCONTROL_EXPORTS
    #define MARCCONTROL_API __declspec(dllexport)
#else
    #define MARCCONTROL_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Main entry point - Runs the application
 * @param argc Argument count
 * @param argv Argument values
 * @return Application exit code
 */
MARCCONTROL_API int runApplication(int argc, char* argv[]);

/**
 * @brief Get DLL version string
 * @return Version string (e.g., "4.1.0")
 */
MARCCONTROL_API const char* getVersion();

/**
 * @brief Get DLL build date and time
 * @return Build timestamp
 */
MARCCONTROL_API const char* getBuildDate();

/**
 * @brief Get product name
 * @return Product name string
 */
MARCCONTROL_API const char* getProductName();

/**
 * @brief Get company/developer name
 * @return Company name
 */
MARCCONTROL_API const char* getCompanyName();

/**
 * @brief Check if DLL is compatible with launcher version
 * @param launcherVersion Launcher version string
 * @return 1 if compatible, 0 if not
 */
MARCCONTROL_API int isCompatible(const char* launcherVersion);

/**
 * @brief Initialize DLL (called before runApplication)
 * @return 1 if successful, 0 if failed
 */
MARCCONTROL_API int initializeDLL();

/**
 * @brief Shutdown DLL (called after runApplication)
 */
MARCCONTROL_API void shutdownDLL();

#ifdef __cplusplus
}
#endif

#endif // MARCCONTROL_H
