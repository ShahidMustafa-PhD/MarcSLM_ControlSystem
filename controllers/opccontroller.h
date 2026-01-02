#ifndef OPCCONTROLLER_H
#define OPCCONTROLLER_H

#include <QObject>
#include "opcserver/opcserverua.h"

class QTextEdit;

/**
 * @brief OPCController - Handles all OPC UA communication and operations
 * 
 * Responsibilities:
 * - OPC UA server initialization and connection management
 * - Reading/writing OPC UA tags
 * - Data buffering and synchronization
 * - Connection monitoring
 */
class OPCController : public QObject {
    Q_OBJECT

public:
    explicit OPCController(QTextEdit* logWidget, QObject* parent = nullptr);
    ~OPCController();

    // Initialization
    bool initialize();
    bool isInitialized() const;
    
    // OPC Write Operations
    bool writeStartUp(bool value);
    bool writePowderFillParameters(int layers, int deltaSource, int deltaSink);
    bool writeLayerParameters(int layers, int deltaSource, int deltaSink);
    bool writeBottomLayerParameters(int layers, int deltaSource, int deltaSink);
    bool writeEmergencyStop();
    bool writeCylinderPosition(bool isSource, int position);
    
    // OPC Read Operations
    bool readData();  // Reads all data and emits dataUpdated signal
    const OPCServerManagerUA::OPCData& currentData() const { return mCurrentData; }
    OPCServerManagerUA* getOPCServerManager() const { return mOPCServer; } // Get OPCServerManagerUA for direct access

signals:
    void dataUpdated(const OPCServerManagerUA::OPCData& data);
    void connectionLost();
    void statusMessage(const QString& msg);
    void errorMessage(const QString& msg);

private slots:
    void onOPCDataUpdated(const OPCServerManagerUA::OPCData& data);
    void onOPCConnectionLost();
    void onOPCLogMessage(const QString& message);

private:
    OPCServerManagerUA* mOPCServer;
    OPCServerManagerUA::OPCData mCurrentData;
    QTextEdit* mLogWidget;
    
    void log(const QString& message);
};

#endif // OPCCONTROLLER_H
