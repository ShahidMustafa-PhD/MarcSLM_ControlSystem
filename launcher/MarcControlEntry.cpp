// ============================================================================
// MarcControl DLL Entry Point
// Single DLL containing all application logic
// ============================================================================

#include "mainwindow.h"
#include "ProjectManager.h"
#include <QApplication>
#include <QString>
#include <QDebug>

// Export macro
#ifdef MARCCONTROL_EXPORTS
    #define MARCCONTROL_API __declspec(dllexport)
#else
    #define MARCCONTROL_API __declspec(dllimport)
#endif

// Global application instance
static QApplication* g_app = nullptr;
static MainWindow* g_mainWindow = nullptr;

// ============================================================================
// Exported Functions (C ABI for stability)
// ============================================================================

extern "C" {

/**
 * @brief Main entry point - Runs the application
 * @param argc Argument count
 * @param argv Argument values
 * @return Application exit code
 */
MARCCONTROL_API int runApplication(int argc, char* argv[]) {
    try {
        // Create QApplication instance
        g_app = new QApplication(argc, argv);
        g_app->setApplicationName("MarcSLM Control System");
        g_app->setOrganizationName("Shahid Mustafa");
        g_app->setApplicationVersion(QString("%1.%2.%3")
            .arg(4).arg(1).arg(0)); // Version from cmake/Version.cmake
        
        qDebug() << "MarcControl.dll: Application initialized";
        
        // Create and show main window
        g_mainWindow = new MainWindow();
        g_mainWindow->show();
        
        qDebug() << "MarcControl.dll: Main window created and shown";
        
        // Run event loop
        int result = g_app->exec();
        
        qDebug() << "MarcControl.dll: Application exiting with code:" << result;
        
        // Cleanup
        delete g_mainWindow;
        g_mainWindow = nullptr;
        
        delete g_app;
        g_app = nullptr;
        
        return result;
        
    } catch (const std::exception& e) {
        qCritical() << "MarcControl.dll: Exception in runApplication:" << e.what();
        return -1;
    } catch (...) {
        qCritical() << "MarcControl.dll: Unknown exception in runApplication";
        return -1;
    }
}

/**
 * @brief Get DLL version string
 * @return Version string (e.g., "4.1.0")
 */
MARCCONTROL_API const char* getVersion() {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d", 4, 1, 0);
    return version;
}

/**
 * @brief Get DLL build date and time
 * @return Build timestamp
 */
MARCCONTROL_API const char* getBuildDate() {
    static char buildDate[64];
    snprintf(buildDate, sizeof(buildDate), "%s %s", __DATE__, __TIME__);
    return buildDate;
}

/**
 * @brief Get product name
 * @return Product name string
 */
MARCCONTROL_API const char* getProductName() {
    return "MarcSLM Control System";
}

/**
 * @brief Get company/developer name
 * @return Company name
 */
MARCCONTROL_API const char* getCompanyName() {
    return "Shahid Mustafa";
}

/**
 * @brief Check if DLL is compatible with launcher version
 * @param launcherVersion Launcher version string
 * @return 1 if compatible, 0 if not
 */
MARCCONTROL_API int isCompatible(const char* launcherVersion) {
    // Always compatible for now
    // Can add version checking logic later
    return 1;
}

/**
 * @brief Initialize DLL (called before runApplication)
 * @return 1 if successful, 0 if failed
 */
MARCCONTROL_API int initializeDLL() {
    qDebug() << "MarcControl.dll: initializeDLL() called";
    // Pre-initialization logic if needed
    return 1;
}

/**
 * @brief Shutdown DLL (called after runApplication)
 */
MARCCONTROL_API void shutdownDLL() {
    qDebug() << "MarcControl.dll: shutdownDLL() called";
    // Cleanup logic if needed
}

} // extern "C"

// ============================================================================
// DLL Main (Windows)
// ============================================================================

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            qDebug() << "MarcControl.dll: DLL_PROCESS_ATTACH";
            // DLL is being loaded
            break;
            
        case DLL_PROCESS_DETACH:
            qDebug() << "MarcControl.dll: DLL_PROCESS_DETACH";
            // DLL is being unloaded
            break;
            
        case DLL_THREAD_ATTACH:
            // New thread created
            break;
            
        case DLL_THREAD_DETACH:
            // Thread exiting
            break;
    }
    return TRUE;
}
#endif // _WIN32
