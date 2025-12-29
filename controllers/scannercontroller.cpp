#include "scannercontroller.h"
#include <QTextEdit>
#include <QLCDNumber>
#include <QLabel>
#include <QMessageBox>

ScannerController::ScannerController(QTextEdit* logWidget, QObject* parent)
    : QObject(parent)
    , mScanner(new Scanner())
    , mLogWidget(logWidget)
    , mLayersProcessed(0)
{
    // Setup scanner logging callback - DISABLE LOGGING FROM WORKER THREAD IN TEST MODE
    // The callback is invoked from the consumer thread while executing Scanner commands
    // Qt's QMetaObject::invokeMethod can cause "resource deadlock would occur" if:
    // 1. The object's thread (GUI thread) is blocked
    // 2. The event queue is full
    // 3. Thread affinity issues prevent proper queuing
    //
    // SOLUTION: Don't emit signals from worker thread logging
    // Instead, suppress callback or use a thread-safe queue
    mScanner->setLogCallback([this](const std::string& message) {
        // CRITICAL FIX: Don't try to emit Qt signals from worker thread
        // This causes Qt's internal deadlock detection to trigger
        // Simply ignore logging from worker threads in test/production mode
        
        // Optional: Use qDebug for minimal logging (no signal emission)
        // qDebug() << "Scanner:" << QString::fromStdString(message);
        
        // Do nothing - suppress worker thread logging to avoid deadlock
    });
}

ScannerController::~ScannerController() {
    if (mScanner) {
        mScanner->shutdown();
        delete mScanner;
        mScanner = nullptr;
    }
}

void ScannerController::log(const QString& message) {
    if (mLogWidget) {
        mLogWidget->append(message);
    }
    emit statusMessage(message);
}

bool ScannerController::initialize() {
    log("Initializing RTC5 Scanner...");
    
    // Configure scanner parameters for pilot mode (zero power)
    Scanner::Config config;
    config.cardNumber = 1;
    config.listMemory = 10000;
    config.markSpeed = 250.0;   // Slow speed for accuracy
    config.jumpSpeed = 1000.0;
    config.laserMode = 1;       // YAG mode
    config.analogOutValue = 0;  // Zero laser power (pilot mode)
    config.analogOutStandby = 0;
    
    if (mScanner->initialize(config)) {
        log("? Scanner initialized successfully");
        log("? Configuration: 20mm square, zero power, pilot mode");
        log("  ? Laser power: 0 (safe testing mode)");
        log("  ? Mark speed: 250 mm/s");
        log("  ? Jump speed: 1000 mm/s");
        
        mLayersProcessed = 0;
        return true;
    } else {
        log("? Failed to initialize Scanner");
        emit errorMessage("Failed to initialize RTC5 Scanner.\n"
                         "Check that:\n"
                         "- RTC5 card is installed\n"
                         "- RTC5DLL.DLL is present\n"
                         "- Correction files are in working directory");
        return false;
    }
}

bool ScannerController::isInitialized() const {
    return mScanner && mScanner->isInitialized();
}

void ScannerController::shutdown() {
    if (mScanner) {
        mScanner->shutdown();
        log("Scanner shutdown complete");
    }
}

bool ScannerController::runDiagnostics() {
    if (!isInitialized()) {
        log("?? Cannot run diagnostics - scanner not initialized");
        emit errorMessage("Scanner is not initialized.\nPlease initialize scanner first.");
        return false;
    }
    
    log("\n=== Running Scanner Diagnostics ===");
    
    Scanner::ScannerStatus status = mScanner->getDetailedStatus();
    
    log(QString("Scanner Status:"));
    log(QString("  • Busy: %1").arg(status.isBusy ? "Yes" : "No"));
    log(QString("  • List Position: %1").arg(status.listPosition));
    log(QString("  • Input Pointer: %1").arg(status.inputPointer));
    log(QString("  • Error Code: %1").arg(status.error));
    
    if (status.error != 0) {
        log(QString("  ?? Error detected: %1").arg(mScanner->getErrorMessage().c_str()));
        emit scannerError(status.error, QString::fromStdString(mScanner->getErrorMessage()));
    } else {
        log("  ? No errors detected");
    }
    
    // Test basic operations
    log("\nTesting Basic Operations:");
    
    UINT listSpace; //= mScanner->getListSpace();
    log(QString("  • Available List Space: %1").arg(listSpace));
    
    long x = 0, y = 0;
    if (mScanner->getCurrentPosition(x, y)) {
        log(QString("  • Current Position: X=%1, Y=%2").arg(x).arg(y));
    } else {
        log("  ?? Could not read current position");
    }
    
    log("\n=== Diagnostics Complete ===\n");
    return true;
}

bool ScannerController::drawPilotSquare(long centerX, long centerY, long sizeHalf) {
    if (!isInitialized()) {
        log("?? Cannot draw square - scanner not initialized");
        return false;
    }
    
    // Define square corners
    Scanner::Point points[5];
    points[0] = Scanner::Point(centerX - sizeHalf, centerY - sizeHalf);  // Bottom-left
    points[1] = Scanner::Point(centerX + sizeHalf, centerY - sizeHalf);  // Bottom-right
    points[2] = Scanner::Point(centerX + sizeHalf, centerY + sizeHalf);  // Top-right
    points[3] = Scanner::Point(centerX - sizeHalf, centerY + sizeHalf);  // Top-left
    points[4] = Scanner::Point(centerX - sizeHalf, centerY - sizeHalf);  // Close square
    
    // Jump to start position
    if (!mScanner->jumpTo(points[0])) {
        log("? Failed to jump to start position");
        return false;
    }
    
    // Draw the four sides
    for (int i = 1; i < 5; i++) {
        if (!mScanner->markTo(points[i])) {
            log(QString("? Failed to mark to point %1").arg(i));
            return false;
        }
    }
    
    // Execute the list
    if (!mScanner->executeList()) {
        log("? Failed to execute scanner list");
        return false;
    }
    
    return true;
}

bool ScannerController::performLayerScanningTest() {
    if (!isInitialized()) {
        log("?? Scanner not ready for layer scanning");
        return false;
    }
    
    log(QString("? Layer %1: Drawing pilot square (zero power)").arg(mLayersProcessed + 1));
    
    // Draw 20mm square centered at origin (0,0)
    const long SQUARE_HALF_SIZE = 10000;  // 10mm = 10000 microns
    const long CENTER_X = 0;
    const long CENTER_Y = 0;
    
    if (drawPilotSquare(CENTER_X, CENTER_Y, SQUARE_HALF_SIZE)) {
        // Wait for scanner to complete
        //if (mScanner->waitForListCompletion(5000)) {
           if (0) {
            mLayersProcessed++;
            log(QString("? Layer %1 completed successfully").arg(mLayersProcessed));
            log(QString("  ? Total layers processed: %1/%2")
                .arg(mLayersProcessed).arg(MAX_PILOT_LAYERS));
            
            emit layerCompleted(mLayersProcessed);
            return true;
        } else {
            log("? Scanner timeout waiting for completion");
            handleError();
            return false;
        }
    } else {
        log("? Failed to draw pilot square");
        handleError();
        return false;
    }
}

void ScannerController::setLaserPower(UINT power) {
    if (!isInitialized()) {
        log("?? Cannot set laser power - scanner not initialized");
        return;
    }
    
    if (mScanner->setLaserPowerList(power)) {
        log(QString("? Laser power set to %1").arg(power));
    } else {
        log("? Failed to set laser power");
    }
}

void ScannerController::setSpeeds(double markSpeed, double jumpSpeed) {
    if (!isInitialized()) {
        log("?? Cannot set speeds - scanner not initialized");
        return;
    }
    
    if (mScanner->setMarkSpeedList(markSpeed) && 
        mScanner->setJumpSpeedList(jumpSpeed)) {
        log(QString("? Speeds updated: Mark=%1, Jump=%2")
            .arg(markSpeed).arg(jumpSpeed));
    } else {
        log("? Failed to set speeds");
    }
}

void ScannerController::setWobble(bool enable, UINT amplitude, double frequency) {
    if (!isInitialized()) {
        log("?? Cannot configure wobble - scanner not initialized");
        return;
    }
    
    if (enable) {
        if (mScanner->setWobble(amplitude, static_cast<UINT>(amplitude * 0.6), frequency)) {
            log(QString("? Wobble enabled: %1 microns @ %2 Hz")
                .arg(amplitude).arg(frequency));
        } else {
            log("? Failed to enable wobble");
        }
    } else {
        if (mScanner->disableWobble()) {
            log("? Wobble disabled");
        } else {
            log("? Failed to disable wobble");
        }
    }
}

void ScannerController::updateStatusDisplay(QLCDNumber* display, QLabel* errorLabel) {
    if (!isInitialized() || !display || !errorLabel) {
        return;
    }
    
    Scanner::ScannerStatus status = mScanner->getDetailedStatus();
    
    display->display(static_cast<int>(status.error));
    
    if (status.error != 0) {
        errorLabel->setText(QString::fromStdString(mScanner->getErrorMessage()));
        errorLabel->setStyleSheet("QLabel { color: #F44336; font-size: 9pt; }");
    } else {
        errorLabel->setText("No errors");
        errorLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 9pt; }");
    }
}

Scanner::ScannerStatus ScannerController::getStatus() {
    if (isInitialized()) {
        return mScanner->getDetailedStatus();
    }
    
    // Return default status if not initialized
    Scanner::ScannerStatus status;
    status.isBusy = 0;
    status.listPosition = 0;
    status.inputPointer = 0;
    status.error = 9999;  // Not initialized
    status.encoderX = 0;
    status.encoderY = 0;
    return status;
}

void ScannerController::handleError() {
    if (!mScanner) {
        return;
    }
    
    UINT errorCode = mScanner->getLastError();
    std::string errorMsg = mScanner->getErrorMessage();
    
    log(QString("?? Scanner Error %1: %2")
        .arg(errorCode)
        .arg(QString::fromStdString(errorMsg)));
    
    emit scannerError(errorCode, QString::fromStdString(errorMsg));
    
    // Try to reset error
    if (mScanner->resetError()) {
        log("? Scanner error cleared");
    } else {
        log("?? Could not clear scanner error - may require reinitialization");
    }
}

void ScannerController::emitLogMessage(const QString& msg) {
    emit logMessage(msg);
}
