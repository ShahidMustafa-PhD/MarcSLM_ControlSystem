#include "opcserver.h"

#include <QtWidgets/QMessageBox>
#include <cmath>

// ============================================================================
// OPCServerManager Implementation
// ============================================================================

OPCServerManager::OPCServerManager(QObject* parent)
    : QObject(parent)
{
    VariantInit(&mVar);
}

OPCServerManager::~OPCServerManager() {
    VariantClear(&mVar);
    
    // Cleanup will be handled by OPC library
    // Groups and items are managed by COPCServer
}

void OPCServerManager::setLogCallback(std::function<void(const QString&)> callback) {
    mLogCallback = callback;
}

void OPCServerManager::log(const QString& message) {
    if (mLogCallback) {
        mLogCallback(message);
    }
    emit logMessage(message);
}

bool OPCServerManager::initialize() {
    try {
        log("Connecting to OPC Server...");

        COPCClient::init();

        // Connect to OPC server
        // Allow overriding host and ProgID via environment variables for interactive testing
        QByteArray envHost = qgetenv("OPC_HOST");
        QByteArray envProgId = qgetenv("OPC_DA_PROGID");

        CString hostName;
        if (!envHost.isEmpty()) {
            QString hostStr = QString::fromLocal8Bit(envHost);
            hostName = hostStr.toStdWString().c_str();
            log(QString("OPC host from OPC_HOST: %1").arg(hostStr));
        } else {
            hostName = "localhost"; // default when server is on the same PC
            log("OPC host defaulting to localhost");
        }

        CString serverName;
        if (!envProgId.isEmpty()) {
            QString progStr = QString::fromLocal8Bit(envProgId);
            serverName = progStr.toStdWString().c_str();
            log(QString("OPC DA ProgID from OPC_DA_PROGID: %1").arg(progStr));
        } else {
            serverName = "CoDeSys.OPC.DA"; // default CoDeSys DA ProgID
            log("OPC DA ProgID defaulting to CoDeSys.OPC.DA");
        }

        COPCHost* host = COPCClient::makeHost(hostName);

        if (!host) {
            log("ERROR: Failed to create OPC host");
            return false;
        }

        log("OPC Host initialized");

        // List available servers
        CAtlArray<CString> localServerList;
        host->getListOfDAServers(IID_CATID_OPCDAServer20, localServerList);

        log(QString("Found %1 OPC servers").arg(localServerList.GetCount()));

        for (unsigned i = 0; i < localServerList.GetCount(); i++) {
            log(QString::fromUtf8(localServerList[i].GetString()));
        }

        // Connect to CoDeSys OPC server (or the overridden ProgID)
        COPCServer* opcServer = host->connectDAServer(serverName);

        if (!opcServer) {
            log(QString("ERROR: Failed to connect to OPC server: %1")
                .arg(QString::fromUtf8(serverName.GetString())));
            log("Application will run without OPC functionality");
            return false;
        }

        // Check server status
        ServerStatus status;
        opcServer->getStatus(status);
        log(QString("Server state is %1").arg(status.dwServerState));
        log(QString("OPC Server connected (state: %1)").arg(status.dwServerState));

        // Browse server items
        log("Server Item Names:");
        CAtlArray<CString> opcItemNames;
        opcServer->getItemNames(opcItemNames);
        log(QString("Got %1 names").arg(opcItemNames.GetCount()));

        for (unsigned i = 0; i < opcItemNames.GetCount(); i++) {
            log(QString::fromUtf8(opcItemNames[i].GetString()));
        }

        // Setup OPC groups
        setupOPCGroups(opcServer);

        log("OPC Server initialized successfully");
        mIsInitialized = true;

        return true;
    }
    catch (const std::exception& e) {
        log(QString("EXCEPTION during OPC init: %1").arg(e.what()));
        return false;
    }
    catch (...) {
        log("UNKNOWN EXCEPTION during OPC init");
        return false;
    }
}

void OPCServerManager::setupOPCGroups(COPCServer* opcServer) {
    if (!opcServer) {
        return;
    }

    try {
        unsigned long refreshRate;

        // Setup default group
        mGroup_Default = opcServer->makeGroup("group_Default", true, 1000, refreshRate, 0.0);
        if (mGroup_Default) {
            mName_StartUp.SetString(_T("CECC.MaTe_DLMS.StartUpSequence.StartUp"));
            mItem_StartUp = mGroup_Default->addItem(mName_StartUp, true);
            if (!mItem_StartUp) {
                log("WARNING: Failed to create StartUp item");
            }
        }

        // Setup async read group
        mGroup_AsynchRead = opcServer->makeGroup("group_AsynchRead", true, 200, refreshRate, 0.0);
        if (mGroup_AsynchRead) {
            setupAsyncReadGroup();
        }

        // Setup layers group
        mGroup_Layers = opcServer->makeGroup("group_Layers", true, 1000, refreshRate, 0.0);
        if (mGroup_Layers) {
            setupLayersGroup();
        }
    }
    catch (...) {
        log("ERROR: Exception in setupOPCGroups");
    }
}

void OPCServerManager::setupAsyncReadGroup() {
    if (!mGroup_AsynchRead) {
        return;
    }

    try {
        CAtlArray<HRESULT> errors;

        // Add items for async reading
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.MakeSurface.Z_Stacks")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.StartUpSequence.StartUp_Done")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition")));
        mNames_readback.Add(CString(_T("CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done")));

        if (mGroup_AsynchRead->addItems(mNames_readback, mItems_readback, errors, true) != 0) {
            log("Asynch Readback Group: Item create failed");
            log("WARNING: Some OPC items failed to create");
        }
    }
    catch (...) {
        log("ERROR: Exception in setupAsyncReadGroup");
    }
}

void OPCServerManager::setupLayersGroup() {
    if (!mGroup_Layers) {
        return;
    }

    try {
        log("*** Layers Group <- add cylinder feed amounts and total stack count");

        // MakeSurface items
        mName_layersMax.SetString(_T("CECC.MaTe_DLMS.MakeSurface.Z_Stacks"));
        mItem_layersMax = mGroup_Layers->addItem(mName_layersMax, true);

        mName_delta_Source.SetString(_T("CECC.MaTe_DLMS.MakeSurface.Delta_Source"));
        mItem_delta_Source = mGroup_Layers->addItem(mName_delta_Source, true);

        mName_delta_Sink.SetString(_T("CECC.MaTe_DLMS.MakeSurface.Delta_Sink"));
        mItem_delta_Sink = mGroup_Layers->addItem(mName_delta_Sink, true);

        mName_MakeSurface_Done.SetString(_T("CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done"));
        mItem_MakeSurface_Done = mGroup_Layers->addItem(mName_MakeSurface_Done, true);

        mName_Marcer_Source_Cylinder_ActualPosition.SetString(_T("CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition"));
        mItem_Marcer_Source_Cylinder_ActualPosition = mGroup_Layers->addItem(mName_Marcer_Source_Cylinder_ActualPosition, true);

        mName_Marcer_Sink_Cylinder_ActualPosition.SetString(_T("CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition"));
        mItem_Marcer_Sink_Cylinder_ActualPosition = mGroup_Layers->addItem(mName_Marcer_Sink_Cylinder_ActualPosition, true);

        // GVL items
        mName_StartSurfaces.SetString(_T("CECC.MaTe_DLMS.GVL.StartSurfaces"));
        mItem_StartSurfaces = mGroup_Layers->addItem(mName_StartSurfaces, true);

        mName_g_Marcer_Source_Cylinder_ActualPosition.SetString(_T("CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition"));
        mItem_g_Marcer_Source_Cylinder_ActualPosition = mGroup_Layers->addItem(mName_g_Marcer_Source_Cylinder_ActualPosition, true);

        mName_g_Marcer_Sink_Cylinder_ActualPosition.SetString(_T("CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition"));
        mItem_g_Marcer_Sink_Cylinder_ActualPosition = mGroup_Layers->addItem(mName_g_Marcer_Sink_Cylinder_ActualPosition, true);

        // Prepare2Process items
        mName_LaySurface.SetString(_T("CECC.MaTe_DLMS.Prepare2Process.LaySurface"));
        mItem_LaySurface = mGroup_Layers->addItem(mName_LaySurface, true);

        mName_LaySurface_Done.SetString(_T("CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done"));
        mItem_LaySurface_Done = mGroup_Layers->addItem(mName_LaySurface_Done, true);

        mName_Step_Sink.SetString(_T("CECC.MaTe_DLMS.Prepare2Process.Step_Sink"));
        mItem_Step_Sink = mGroup_Layers->addItem(mName_Step_Sink, true);

        mName_Step_Source.SetString(_T("CECC.MaTe_DLMS.Prepare2Process.Step_Source"));
        mItem_Step_Source = mGroup_Layers->addItem(mName_Step_Source, true);

        mName_Lay_Stacks.SetString(_T("CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks"));
        mItem_Lay_Stacks = mGroup_Layers->addItem(mName_Lay_Stacks, true);

        // StartUpSequence items
        mName_StartUp.SetString(_T("CECC.MaTe_DLMS.StartUpSequence.StartUp"));
        mItem_StartUp = mGroup_Layers->addItem(mName_StartUp, true);

        mName_StartUp_Done.SetString(_T("CECC.MaTe_DLMS.StartUpSequence.StartUp_Done"));
        mItem_StartUp_Done = mGroup_Layers->addItem(mName_StartUp_Done, true);

        // Validate critical items
        int itemCount = 0;
        if (mItem_layersMax) itemCount++;
        if (mItem_delta_Source) itemCount++;
        if (mItem_delta_Sink) itemCount++;
        if (mItem_StartSurfaces) itemCount++;
        
        log(QString("Successfully created %1 critical OPC items in Layers group").arg(itemCount));
    }
    catch (...) {
        log("ERROR: Exception in setupLayersGroup");
    }
}

bool OPCServerManager::readData(OPCData& data) {
    if (!mGroup_AsynchRead) {
        emit connectionLost();
        return false;
    }

    try {
        COPCItem_DataMap opcData;
        mGroup_AsynchRead->readSync(mItems_readback, opcData, OPC_DS_DEVICE);

        POSITION pos = opcData.GetStartPosition();
        int count = opcData.GetCount();

        while (count > 0) {
            COPCItem* key = opcData.GetKeyAt(pos);
            OPCItemData* itemData = opcData.GetNextValue(pos);

            if (!key || !itemData) {
                count--;
                continue;
            }

            CString itemname = key->getName();

            if (itemname == mNames_readback.GetAt(0)) {// CECC.MaTe.MakeSurface.Marcer_Source_Cylinder_ActualPosition
                data.sourceCylPosition = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(1)) {// CECC.MaTe.MakeSurface.Marcer_Sink_Cylinder_ActualPosition
                data.sinkCylPosition = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(2)) {// CECC.MaTe.MakeSurface.Z_Stacks
                data.stacksLeft = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(3)) {// CECC.MaTe.MakeSurface.MakeSurface_Done
                data.ready2Powder = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(4)) {// CECC.MaTe.StartUpSequence.StartUp_Done
                data.startUpDone = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(5)) {// CECC.MaTe.MakeSurface.g_Marcer_Source_Cylinder_ActualPosition
                data.g_sourceCylPosition = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(6)) {// CECC.MaTe.MakeSurface.g_Marcer_Sink_Cylinder_ActualPosition
                data.g_sinkCylPosition = itemData->vDataValue.intVal;
            }
            else if (itemname == mNames_readback.GetAt(7)) {// CECC.MaTe.Prepare2Process.LaySurface_Done
                data.powderSurfaceDone = itemData->vDataValue.intVal;
            }

            count--;
        }

        emit dataUpdated(data);
        return true;
    }
    catch (...) {
        log("ERROR: OPC Read Error Occurred!");
        return false;
    }
}

bool OPCServerManager::writeStartUp(bool value) {
    if (!mItem_StartUp) {
        log("ERROR: StartUp item not initialized");
        return false;
    }

    try {
        mVar.vt = VT_BOOL;
        mVar.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        mItem_StartUp->writeSync(mVar);
        log("Startup command sent to PLC");
        return true;
    }
    catch (...) {
        log("ERROR: Failed to write StartUp command");
        return false;
    }
}

bool OPCServerManager::writePowderFillParameters(int layers, int deltaSource, int deltaSink) {
    if (!mItem_layersMax || !mItem_Lay_Stacks || !mItem_delta_Source ||
        !mItem_delta_Sink || !mItem_StartSurfaces) {
        log("ERROR: Required OPC items not initialized");
        return false;
    }

    try {
        Sleep(100);

        mVar.vt = VT_INT;
        mVar.intVal = layers;
        mItem_layersMax->writeSync(mVar);
        mItem_Lay_Stacks->writeSync(mVar);
        Sleep(100);

        mVar.vt = VT_INT;
        mVar.intVal = deltaSource;
        mItem_delta_Source->writeSync(mVar);
        Sleep(100);

        mVar.vt = VT_INT;
        mVar.intVal = deltaSink;
        mItem_delta_Sink->writeSync(mVar);
        Sleep(100);

        mVar.vt = VT_BOOL;
        mVar.boolVal = VARIANT_TRUE;
        mItem_StartSurfaces->writeSync(mVar);
        Sleep(500);

        log("Powder fill parameters sent to PLC");
        return true;
    }
    catch (...) {
        log("ERROR: Failed to write powder fill parameters");
        return false;
    }
}

bool OPCServerManager::writeLayerParameters(int layers, int deltaSource, int deltaSink) {
    if (!mItem_Lay_Stacks || !mItem_Step_Source || 
        !mItem_Step_Sink || !mItem_LaySurface) {
        log("ERROR: Layer items not initialized");
        return false;
    }

    try {
        mVar.vt = VT_INT;
        mVar.intVal = layers;
        mItem_Lay_Stacks->writeSync(mVar);
        Sleep(100);

        mVar.vt = VT_INT;
        mVar.intVal = deltaSource;
        mItem_Step_Source->writeSync(mVar);
        Sleep(100);

        mVar.vt = VT_INT;
        mVar.intVal = deltaSink;
        mItem_Step_Sink->writeSync(mVar);
        Sleep(100);

        mVar.vt = VT_BOOL;
        mVar.boolVal = VARIANT_TRUE;
        mItem_LaySurface->writeSync(mVar);
        Sleep(400);

        log("Layer parameters sent to PLC");
        return true;
    }
    catch (...) {
        log("ERROR: Failed to write layer parameters");
        return false;
    }
}

bool OPCServerManager::writeBottomLayerParameters(int layers, int deltaSource, int deltaSink) {
    if (!mItem_Lay_Stacks || !mItem_Step_Source ||
        !mItem_Step_Sink || !mItem_LaySurface) {
        log("ERROR: Bottom layer items not initialized");
        return false;
    }

    try {
        mVar.vt = VT_INT;
        mVar.intVal = layers;
        mItem_Lay_Stacks->writeSync(mVar);
		Sleep(1000);// why sleep so long here?

        mVar.vt = VT_INT;
        mVar.intVal = deltaSource;
        mItem_Step_Source->writeSync(mVar);
        Sleep(1000);

        mVar.vt = VT_INT;
        mVar.intVal = deltaSink;
        mItem_Step_Sink->writeSync(mVar);
        Sleep(1000);

        mVar.vt = VT_BOOL;
        mVar.boolVal = VARIANT_TRUE;
        mItem_LaySurface->writeSync(mVar);
        Sleep(500);

        log("Bottom layer parameters sent to PLC");
        return true;
    }
    catch (...) {
        log("ERROR: Failed to write bottom layer parameters");
        return false;
    }
}

bool OPCServerManager::writeEmergencyStop() {
    try {
        if (mItem_StartSurfaces) {
            mVar.vt = VT_BOOL;
            mVar.boolVal = VARIANT_FALSE;
            mItem_StartSurfaces->writeSync(mVar);
        }
        
        log("⚠️ EMERGENCY STOP signal sent to PLC!");
        return true;
    }
    catch (...) {
        log("ERROR: Failed to send emergency stop signal!");
        return false;
    }
}

bool OPCServerManager::writeCylinderPosition(bool isSource, int position) {
    if (!mIsInitialized) {
        log("OPC not initialized - cannot write cylinder position");
        return false;
    }

    COPCItem* item = isSource ? mItem_Marcer_Source_Cylinder_ActualPosition : mItem_Marcer_Sink_Cylinder_ActualPosition;
    if (!item) {
        log("OPC item for cylinder position not found");
        return false;
    }

    VARIANT var;
    VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = position;

    try {
        item->writeSync(var);
        log(QString("✓ Cylinder position (%1) written: %2")
            .arg(isSource ? "Source" : "Sink").arg(position));
        return true;
    } catch (...) {
        log(QString("✗ Failed to write cylinder position (%1)").arg(isSource ? "Source" : "Sink"));
        return false;
    }
}

// ============================================================================
// writeLayerExecutionComplete - Notify PLC that Scanner finished this layer
// ============================================================================
//
// BIDIRECTIONAL HANDSHAKE COMPLETION:
// ─────────────────────────────────────────────────────────────────────────
// This method completes the per-layer synchronization loop between OPC and Scanner.
//
// Full Cycle:
//   1. Scanner requests layer creation via writeLayerParameters()
//      └─ Writes: Lay_Stacks, Step_Source, Step_Sink, LaySurface=TRUE
//   
//   2. PLC executes layer creation (recoater, platform movement)
//      └─ When complete: Sets LaySurface_Done=TRUE
//   
//   3. ProcessController detects LaySurface_Done (polling)
//      └─ Calls ScanStreamingManager::notifyPLCPrepared()
//      └─ Wakes consumer thread (condition_variable)
//   
//   4. Consumer thread executes laser scanning
//      └─ Reads RTCCommandBlock from queue
//      └─ Executes jump/mark commands on Scanner
//      └─ Turns laser OFF
//   
//   5. Consumer calls notifyLayerExecutionComplete()
//      └─ THIS METHOD is called
//      └─ Resets LaySurface=FALSE (signals PLC: scanner done)
//   
//   6. Loop repeats for next layer
//
// Why Reset LaySurface to FALSE:
// ─────────────────────────────────────────────────────────────────────────
// The PLC monitors LaySurface tag. When we set it FALSE after laser scanning,
// the PLC knows:
//   • Scanner has finished executing this layer
//   • PLC can proceed with next layer if Scanner requests it
//   • Prevents race condition (scanner requests next layer while PLC still busy)
//
// This implements the industrial standard: "Scanner signals completion back to PLC"
//
bool OPCServerManager::writeLayerExecutionComplete(int layerNumber) {
    // ========== VALIDATE OPC INITIALIZED ========= =
    //
    // Cannot signal layer completion if OPC not connected.
    // This is an error condition (should never happen in production).
    //
    if (!mIsInitialized) {
        log("⚠️ OPC not initialized - cannot notify layer execution complete");
        return false;
    }

    // ========== VALIDATE OPC ITEM AVAILABLE ========= =
    //
    // mItem_LaySurface is the tag that PLC monitors.
    // Setting it to FALSE signals: "Scanner finished layer"
    //
    if (!mItem_LaySurface) {
        log("⚠️ LaySurface OPC item not initialized - cannot notify completion");
        return false;
    }

    try {
        // ========== RESET LaySurface TAG TO FALSE ========= =
        //
        // This signals PLC that Scanner has completed laser execution.
        // PLC can now proceed with next layer creation when Scanner requests it.
        //
        // Industrial Practice:
        //   • TRUE  = Request layer creation (recoater, platform)
        //   • FALSE = Scanner finished, ready for next layer
        //
        mVar.vt = VT_BOOL;
        mVar.boolVal = VARIANT_FALSE;
        mItem_LaySurface->writeSync(mVar);

        log(QString("✅ Layer %1 execution complete signal sent to PLC (LaySurface=FALSE)")
            .arg(layerNumber));
        
        return true;
    }
    catch (...) {
        log(QString("❌ Failed to signal layer %1 execution complete to PLC")
            .arg(layerNumber));
        return false;
    }
}