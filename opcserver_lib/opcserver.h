#ifndef OPCSERVER_H
#define OPCSERVER_H


#include <windows.h>
#include <atlstr.h>
#include <QObject>
#include <QString>
#include <functional>
#include <atlcoll.h>
#include "lib/opcda.h"
#include "lib/OPCClient.h"
#include "lib/OPCHost.h"
#include "lib/OPCServer.h"
#include "lib/OPCGroup.h"
#include "lib/OPCItem.h"
// Forward declarations
class COPCServer;
class COPCGroup;
class COPCItem;
class MainWindow;

// ============================================================================
// OPCServer Class - Handles all OPC communication
// ============================================================================
class OPCServerManager : public QObject {
    Q_OBJECT

public:
    explicit OPCServerManager(QObject* parent = nullptr);
    ~OPCServerManager();

    // Initialization
    bool initialize();
    bool isInitialized() const { return mIsInitialized; }

    // OPC Operations
    bool writeStartUp(bool value);
    bool writePowderFillParameters(int layers, int deltaSource, int deltaSink);
    bool writeLayerParameters(int layers, int deltaSource, int deltaSink);
    bool writeBottomLayerParameters(int layers, int deltaSource, int deltaSink);
    bool writeEmergencyStop();
    bool writeCylinderPosition(bool isSource, int position);
    
    // ========== NEW: Bidirectional Layer Synchronization ==========
    //
    // writeLayerExecutionComplete():
    //   Notifies PLC that laser scanning for current layer is complete.
    //   This allows PLC to proceed with next layer creation (recoater, platform).
    //
    // Purpose:
    //   Implements bidirectional OPC-Scanner handshake:
    //   1. OPC creates layer (recoater, platform) → signals "layer ready"
    //   2. Scanner executes layer (laser scan) → signals "layer done"
    //   3. OPC proceeds with next layer creation
    //
    // Parameters:
    //   layerNumber - The layer number that was just completed
    //
    // Returns:
    //   true if OPC write successful, false otherwise
    //
    // Industrial Standard:
    //   This completes the bidirectional synchronization loop required
    //   for production SLM systems where PLC must wait for laser to finish
    //   before creating the next layer.
    //
    bool writeLayerExecutionComplete(int layerNumber);
    
    // Data reading
    struct OPCData {
        int sourceCylPosition = 0;
        int sinkCylPosition = 0;
        int g_sourceCylPosition = 0;
        int g_sinkCylPosition = 0;
        int stacksLeft = 0;
        int ready2Powder = 0;
        int startUpDone = 0;
        int powderSurfaceDone = 0;
    };

    bool readData(OPCData& data);

    // Logging callback
    void setLogCallback(std::function<void(const QString&)> callback);

signals:
    void logMessage(const QString& message);
    void connectionLost();
    void dataUpdated(const OPCData& data);

private:
    // OPC Setup
    void setupOPCGroups(COPCServer* opcServer);
    void setupAsyncReadGroup();
    void setupLayersGroup();

    // Logging helper
    void log(const QString& message);

    // Initialization state
    bool mIsInitialized = false;

    // OPC Groups
    COPCGroup* mGroup_Default = nullptr;
    COPCGroup* mGroup_AsynchRead = nullptr;
    COPCGroup* mGroup_Layers = nullptr;

    // OPC Items - Default Group
    CString mName_StartUp;
    COPCItem* mItem_StartUp = nullptr;

    // OPC Items - AsynchRead Group
    CAtlArray<CString> mNames_readback;
    CAtlArray<COPCItem*> mItems_readback;

    // OPC Items - Layers Group
    // MakeSurface items
    CString mName_layersMax;
    COPCItem* mItem_layersMax = nullptr;
    CString mName_delta_Source;
    COPCItem* mItem_delta_Source = nullptr;
    CString mName_delta_Sink;
    COPCItem* mItem_delta_Sink = nullptr;
    CString mName_MakeSurface_Done;
    COPCItem* mItem_MakeSurface_Done = nullptr;
    CString mName_Marcer_Source_Cylinder_ActualPosition;
    COPCItem* mItem_Marcer_Source_Cylinder_ActualPosition = nullptr;
    CString mName_Marcer_Sink_Cylinder_ActualPosition;
    COPCItem* mItem_Marcer_Sink_Cylinder_ActualPosition = nullptr;

    // GVL items
    CString mName_StartSurfaces;
    COPCItem* mItem_StartSurfaces = nullptr;
    CString mName_g_Marcer_Source_Cylinder_ActualPosition;
    COPCItem* mItem_g_Marcer_Source_Cylinder_ActualPosition = nullptr;
    CString mName_g_Marcer_Sink_Cylinder_ActualPosition;
    COPCItem* mItem_g_Marcer_Sink_Cylinder_ActualPosition = nullptr;

    // Prepare2Process items
    CString mName_LaySurface;
    COPCItem* mItem_LaySurface = nullptr;
    CString mName_LaySurface_Done;
    COPCItem* mItem_LaySurface_Done = nullptr;
    CString mName_Step_Sink;
    COPCItem* mItem_Step_Sink = nullptr;
    CString mName_Step_Source;
    COPCItem* mItem_Step_Source = nullptr;
    CString mName_Lay_Stacks;
    COPCItem* mItem_Lay_Stacks = nullptr;

    // StartUpSequence items
    CString mName_StartUp_Done;
    COPCItem* mItem_StartUp_Done = nullptr;

    // VARIANT for writing
    VARIANT mVar;

    // Log callback
    std::function<void(const QString&)> mLogCallback;
};

#endif // OPCSERVER_H