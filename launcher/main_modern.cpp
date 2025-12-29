#include "ControlEngineThread.h"
#include "MainWindowRefactored.h"
#include <QApplication>
#include <memory>
#include <QDebug>

/**
 * LAUNCHER FOR MODERN CLEAN ARCHITECTURE VERSION
 * 
 * This is the entry point for the refactored MarcSLM system.
 * Compare this to the legacy launcher/main.cpp - much simpler!
 * 
 * ARCHITECTURE:
 * 1. Create QApplication
 * 2. Create ControlEngineThread (starts worker thread)
 * 3. Create MainWindow (connects to engine)
 * 4. Show GUI
 * 5. Run event loop
 * 6. Cleanup via RAII when closed
 * 
 * KEY BENEFITS:
 * - No blocking on GUI thread
 * - Clean separation of concerns
 * - Testable components
 * - Easy to extend
 */

int main(int argc, char* argv[]) {
    // Initialize Qt Application
    QApplication app(argc, argv);
    
    qDebug() << "=== MarcSLM Clean Architecture System ===";
    qDebug() << "GUI Thread ID:" << QThread::currentThread();
    
    // Step 1: Create control engine thread
    // This immediately spawns a worker thread and creates ControlEngine
    qDebug() << "Creating ControlEngineThread...";
    auto engineThread = std::make_unique<ControlEngineThread>();
    qDebug() << "ControlEngine running on worker thread";
    
    // Step 2: Create main window
    // This creates the GUI and connects to engine signals/slots
    qDebug() << "Creating MainWindow...";
    auto mainWindow = std::make_unique<MainWindow>();
    
    // Step 3: Show GUI
    mainWindow->show();
    qDebug() << "System initialized and ready";
    
    // Step 4: Run event loop
    // GUI and worker thread run concurrently
    // - GUI thread handles user input
    // - Worker thread handles business logic/hardware
    // - Signals/slots manage communication
    int result = app.exec();
    
    // Step 5: Cleanup (automatic via destructors)
    qDebug() << "Shutting down...";
    mainWindow.reset();  // Explicit cleanup order
    engineThread.reset();  // Wait for worker thread gracefully
    qDebug() << "Goodbye!";
    
    return result;
}
