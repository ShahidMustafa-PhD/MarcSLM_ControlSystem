#ifndef SCANNERCONTROLLER_H
#define SCANNERCONTROLLER_H

#include <QObject>
#include "Scanner.h"

class QTextEdit;
class QLCDNumber;
class QLabel;

/**
 * @brief ScannerController - Handles all RTC5 Scanner operations
 * 
 * Responsibilities:
 * - Scanner initialization and configuration
 * - Laser control and parameters
 * - Drawing operations (pilot squares, vectors)
 * - Layer-by-layer scanning coordination
 * - Status monitoring and diagnostics
 */
class ScannerController : public QObject {
    Q_OBJECT

public:
    explicit ScannerController(QTextEdit* logWidget, QObject* parent = nullptr);
    ~ScannerController();

    // Initialization
    bool initialize();
    bool isInitialized() const;
    void shutdown();
    
    // Scanner Operations
    bool runDiagnostics();
    bool drawPilotSquare(long centerX, long centerY, long sizeHalf);
    bool performLayerScanningTest();
    
    // Configuration
    void setLaserPower(UINT power);
    void setSpeeds(double markSpeed, double jumpSpeed);
    void setWobble(bool enable, UINT amplitude, double frequency);
    
    // Status & Monitoring
    void updateStatusDisplay(QLCDNumber* display, QLabel* errorLabel);
    Scanner::ScannerStatus getStatus();
    
    // Layer tracking
    int layersProcessed() const { return mLayersProcessed; }
    void resetLayerCount() { mLayersProcessed = 0; }
    int maxPilotLayers() const { return MAX_PILOT_LAYERS; }

    // Add this line:
    void emitLogMessage(const QString& msg);
signals:
    void layerCompleted(int layerNumber);
    void scannerError(UINT errorCode, const QString& message);
    void statusMessage(const QString& msg);
    void errorMessage(const QString& msg);
    void logMessage(const QString& msg);

private:
    Scanner* mScanner;
    QTextEdit* mLogWidget;
    int mLayersProcessed;
    
    static constexpr int MAX_PILOT_LAYERS = 20;
    
    void log(const QString& message);
    void handleError();
};

#endif // SCANNERCONTROLLER_H
