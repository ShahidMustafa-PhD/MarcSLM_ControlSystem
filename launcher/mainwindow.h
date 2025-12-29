#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include "ProjectManager.h"
#include "opcserver_lib/opcserverua.h"  // Include for OPCServerManagerUA and OPCData type
#include <QDockWidget>
#include <QTreeWidget>
#include "controllers/scanstreamingmanager.h"

// Forward declarations - Controllers
class OPCController;
class ScannerController;
class ProcessController;
class SLMWorkerManager;

// Forward declarations - UI
class QPushButton;
class QTextEdit;
class QLCDNumber;
class QDoubleSpinBox;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void startScanProcess(const QString& marcPath);

private slots:
    // Button handlers - delegate to controllers
    void on_InitOPC_clicked();
    void on_InitScanner_clicked();
    void on_StartUP_clicked();
    void on_Prep_Powder_Fill_clicked();
    void on_Lay_Surface_clicked();
    void on_MakeBottomLayers_clicked();
    void on_Restart_process_clicked();
    void on_EmergencyStop_clicked();
    void on_RunScannerDiagnostics_clicked();
    
    // ========== NEW: DUAL SLM PROCESS MODES ==========
    void onTestSLMProcess_clicked();  // Test mode: synthetic layers, no OPC
    void onStartScanProcess_clicked();  // Production mode: slice-file driven

    // Controller signal handlers
    void onOPCDataUpdated(const OPCServerManagerUA::OPCData& data);
    void onOPCConnectionLost();
    void onScannerLayerCompleted(int layerNumber);
    void onProcessStateChanged(int state);
    void onScanProcessStatusMessage(const QString& msg);
    void onScanProcessProgress(size_t processed, size_t total);
    void onScanProcessFinished();
    void onScanProcessError(const QString& err);

    // Menu bar slots
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileExport();
    void onFileExit();
    
    void onEditPreferences();
    void onEditClearLog();
    void onEditResetParameters();
    
    void onViewFullScreen();
    void onViewStatusBar();
    void onViewExpandLog();
    void onViewGenerateSVGs(); // New: generate SVGs from MARC
    
    void onRunInitialize();
    void onRunStart();  // Start process (streaming MARC)
    void onRunPause();
    void onRunStop();
    void onRunEmergencyStop();
    
    void onHelpDocumentation();
    void onHelpAbout();
    void onHelpCheckUpdates();

    // Project actions
    void onProjectOpen();
    void onProjectAttachMarc();
    void onProjectAttachJson();

private:
    void setupUI();
    void setupMenuBar();
    void connectControllerSignals();  // Wire controllers to UI
    void updateProjectExplorer();     // Update project tree view
    
    // Helper for cylinder position updates
    void updateDisplaysFromOPCData(const OPCServerManagerUA::OPCData& data);

    // UI Components
    QPushButton* InitOPC;
    QPushButton* InitScanner;
    QPushButton* StartUP;
    QPushButton* Test_Slm_process;
    QPushButton* Prep_Powder_Fill;
    QPushButton* Lay_Surface;
    QPushButton* MakeBottomLayers;
    QPushButton* EmergencyStop;
    
    QTextEdit* textEdit;
    
    // Scanner UI Controls
    QDoubleSpinBox* laserPowerSpinBox;
    QDoubleSpinBox* markSpeedSpinBox;
    QDoubleSpinBox* jumpSpeedSpinBox;
    QDoubleSpinBox* wobbleAmplitudeSpinBox;
    QDoubleSpinBox* wobbleFrequencySpinBox;
    QDoubleSpinBox* svgScaleSpinBox;
    QPushButton* applyLaserPowerBtn;
    QPushButton* applySpeedBtn;
    QPushButton* enableWobbleBtn;
    QLCDNumber* scannerStatusDisplay;
    QLabel* scannerErrorLabel;
    QPushButton* runDiagnosticsBtn;
    
    // ========== NEW: DUAL SLM PROCESS BUTTONS ==========
    QPushButton* testSLMProcessBtn;   // Test mode: synthetic, no OPC
    QPushButton* startScanProcessBtn;  // Production mode: slice file
    
    QLCDNumber* sourceCylPos;
    QLCDNumber* sinkCylPos;
    QLCDNumber* g_sourceCylPos;
    QLCDNumber* g_sinkCylPos;
    QLCDNumber* stacksLeft;
    QLCDNumber* Ready2Powder;
    QLCDNumber* StartUpDone;
    QLCDNumber* PowderSurfaceDone_2;
    
    QDoubleSpinBox* deltaSource;
    QDoubleSpinBox* deltaSink;
    QDoubleSpinBox* noOfStacks;
    QDoubleSpinBox* deltaSource_BottomLayer;
    QDoubleSpinBox* deltaSink_BottomLayer;
    QDoubleSpinBox* noOfStacks_BottomLayer;

    // Menu bar actions
    QAction* actionNew;
    QAction* actionOpen;
    QAction* actionSave;
    QAction* actionSaveAs;
    QAction* actionExport;
    QAction* actionExit;
    
    QAction* actionPreferences;
    QAction* actionClearLog;
    QAction* actionResetParameters;
    
    QAction* actionFullScreen;
    QAction* actionStatusBar;
    QAction* actionExpandLog;
    
    QAction* actionInitialize;
    QAction* actionStart;
    QAction* actionPause;
    QAction* actionStop;
    QAction* actionEmergencyStop;
    
    QAction* actionDocumentation;
    QAction* actionAbout;
    QAction* actionCheckUpdates;
    QAction* actionGenerateSVGs = nullptr; // New action for generating SVGs

    // Controllers - Business logic delegated here
    OPCController* mOPCController;
    ScannerController* mScannerController;
    ProcessController* mProcessController;
    ProjectManager* mProjectManager;
    ScanStreamingManager* mScanManager;  // NEW: streaming MARC -> RTC
    SLMWorkerManager* mSLMWorkerManager;  // NEW: Industrial threading model

    // Dock widgets
    QDockWidget* projectDock = nullptr;
    QTreeWidget* projectTree = nullptr;

    // Recent projects submenu
    QMenu* recentMenu = nullptr;
    QList<QAction*> recentActions;
    
    // UI State variables only
    bool isFullScreen = false;
    bool isStatusBarVisible = true;
};

#endif // MAINWINDOW_H
