// ============================================================================
// MarcSLM Launcher - Minimal Executable
// Loads and runs MarcControl.dll
// ============================================================================

#include <QApplication>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QString>
#include <QLibrary>

#ifdef _WIN32
#include <windows.h>
#endif

// Function pointer types for DLL functions
typedef int (*RunApplicationFunc)(int, char**);
typedef const char* (*GetVersionFunc)();
typedef const char* (*GetBuildDateFunc)();
typedef const char* (*GetProductNameFunc)();
typedef int (*InitializeDLLFunc)();
typedef void (*ShutdownDLLFunc)();

// ============================================================================
// Update Management
// ============================================================================

bool checkForUpdates(const QString& dllPath) {
    QString updatePath = QDir::currentPath() + "/updates/MarcControl.dll.new";
    QString oldPath = dllPath + ".old";
    
    if (QFileInfo::exists(updatePath)) {
        qDebug() << "Update found:" << updatePath;
        
        // Backup current DLL
        if (QFileInfo::exists(oldPath)) {
            QFile::remove(oldPath);
        }
        
        if (QFileInfo::exists(dllPath)) {
            if (!QFile::rename(dllPath, oldPath)) {
                qWarning() << "Failed to backup current DLL";
                return false;
            }
            qDebug() << "Current DLL backed up to:" << oldPath;
        }
        
        // Move new DLL into place
        if (!QFile::rename(updatePath, dllPath)) {
            qCritical() << "Failed to apply update - restoring backup";
            // Restore backup
            if (QFileInfo::exists(oldPath)) {
                QFile::rename(oldPath, dllPath);
            }
            return false;
        }
        
        qDebug() << "Update applied successfully";
        
        // Show update notification - create temporary QApplication
        {
            int tempArgc = 1;
            char* tempArgv[] = { const_cast<char*>("MarcSLM_Launcher") };
            QApplication tempApp(tempArgc, tempArgv);
            QMessageBox::information(nullptr, "Update Applied",
                "MarcControl.dll has been updated to a new version.\n\n"
                "The application will now start with the new version.",
                QMessageBox::Ok);
        }
        
        return true;
    }
    
    return false;
}

// ============================================================================
// DLL Loading
// ============================================================================

int main(int argc, char* argv[]) {
    qDebug() << "====================================================";
    qDebug() << "MarcSLM Control System Launcher v1.0.0";
    qDebug() << "Developer: Shahid Mustafa";
    qDebug() << "====================================================";
    
    // DLL path
    QString dllPath = QDir::currentPath() + "/MarcControl.dll";
    qDebug() << "DLL path:" << dllPath;
    
    // Check for updates first (may need QApplication for dialog)
    bool updateApplied = checkForUpdates(dllPath);
    if (updateApplied) {
        qDebug() << "Update applied, reloading DLL";
    }
    
    // Verify DLL exists
    if (!QFileInfo::exists(dllPath)) {
        QString errorMsg = QString(
            "MarcControl.dll not found!\n\n"
            "Expected location:\n%1\n\n"
            "Please reinstall the application."
        ).arg(dllPath);
        
        qCritical() << errorMsg;
        
        // Create temporary QApplication for error dialog
        QApplication tempApp(argc, argv);
        QMessageBox::critical(nullptr, "Launch Error", errorMsg);
        return 1;
    }
    
    qDebug() << "MarcControl.dll found";
    
    // Load DLL using QLibrary
    QLibrary dll(dllPath);
    dll.setLoadHints(QLibrary::ResolveAllSymbolsHint);
    
    if (!dll.load()) {
        QString errorMsg = QString(
            "Failed to load MarcControl.dll\n\n"
            "Error: %1\n\n"
            "Please reinstall the application."
        ).arg(dll.errorString());
        
        qCritical() << errorMsg;
        
        // Create temporary QApplication for error dialog
        QApplication tempApp(argc, argv);
        QMessageBox::critical(nullptr, "Launch Error", errorMsg);
        return 1;
    }
    
    qDebug() << "MarcControl.dll loaded successfully";
    
    // Get function pointers
    RunApplicationFunc runApp = (RunApplicationFunc)dll.resolve("runApplication");
    GetVersionFunc getVersion = (GetVersionFunc)dll.resolve("getVersion");
    GetBuildDateFunc getBuildDate = (GetBuildDateFunc)dll.resolve("getBuildDate");
    GetProductNameFunc getProductName = (GetProductNameFunc)dll.resolve("getProductName");
    InitializeDLLFunc initDLL = (InitializeDLLFunc)dll.resolve("initializeDLL");
    ShutdownDLLFunc shutdownDLL = (ShutdownDLLFunc)dll.resolve("shutdownDLL");
    
    if (!runApp) {
        QString errorMsg = 
            "Invalid MarcControl.dll\n\n"
            "The DLL does not contain the required entry point.\n\n"
            "Please reinstall the application.";
        
        qCritical() << errorMsg;
        
        // Create temporary QApplication for error dialog
        QApplication tempApp(argc, argv);
        QMessageBox::critical(nullptr, "Launch Error", errorMsg);
        dll.unload();
        return 1;
    }
    
    // Display version information
    if (getVersion && getBuildDate && getProductName) {
        QString version = QString::fromUtf8(getVersion());
        QString buildDate = QString::fromUtf8(getBuildDate());
        QString productName = QString::fromUtf8(getProductName());
        
        qDebug() << "====================================================";
        qDebug() << "Product:" << productName;
        qDebug() << "Version:" << version;
        qDebug() << "Build Date:" << buildDate;
        qDebug() << "====================================================";
    }
    
    // Initialize DLL
    if (initDLL) {
        if (!initDLL()) {
            qCritical() << "DLL initialization failed";
            
            // Create temporary QApplication for error dialog
            QApplication tempApp(argc, argv);
            QMessageBox::critical(nullptr, "Initialization Error",
                "Failed to initialize MarcControl.dll\n\n"
                "The application cannot start.");
            dll.unload();
            return 1;
        }
        qDebug() << "DLL initialized successfully";
    }
    
    // Run application
    qDebug() << "Starting application...";
    int result = runApp(argc, argv);
    qDebug() << "Application exited with code:" << result;
    
    // Shutdown DLL
    if (shutdownDLL) {
        shutdownDLL();
        qDebug() << "DLL shutdown complete";
    }
    
    // Unload DLL
    dll.unload();
    qDebug() << "DLL unloaded";
    
    qDebug() << "Launcher exiting";
    return result;
}
