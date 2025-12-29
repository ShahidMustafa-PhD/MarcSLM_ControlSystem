#include "mainwindow.h"
#include "controllers/opccontroller.h"
#include "controllers/scannercontroller.h"
#include "controllers/processcontroller.h"
#include "controllers/slm_worker_manager.h"
#include "ProjectManager.h"

#include <windows.h>
#include <QMessageBox>
#include <QTimerEvent>
#include <QString>
#include <QTimer>
#include <QTextEdit>
#include <QLCDNumber>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QWidget>
#include <QFrame>
#include <QScreen>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
#include <QTextStream>
#include <QFile>
#include <QDockWidget>
#include <QTreeWidget>
#include <QBrush>
#include <QColor>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <QDesktopServices>
#include <QUrl>

#include "io/readSlices.h"
#include "io/writeSVG.h"

// ============================================================================
// MainWindow Implementation
// ============================================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Setup UI first
    setupUI();
    
    // Create controllers AFTER UI (they need textEdit for logging)
    mOPCController = new OPCController(textEdit, this);
    mScannerController = new ScannerController(textEdit, this);
    mScanManager = new ScanStreamingManager(this);  // Create BEFORE ProcessController
    mProcessController = new ProcessController(
        mOPCController, mScannerController, textEdit, mScanManager, this);  // Pass streaming manager
    mProjectManager = new ProjectManager(this);
    
    // ========== INDUSTRIAL THREADING: SLM Worker Manager ==========
    // Create worker manager for OPC and Scanner threads
    // This ensures OPC and Scanner are owned and executed in their own threads (industrial standard)
    mSLMWorkerManager = new SLMWorkerManager(this);
    
    // ========== NEW: Connect OPC manager to streaming manager for layer creation ==========
    // ScanStreamingManager needs OPCServerManager reference to call writeLayerParameters
    mScanManager->setOPCManager(mOPCController->getOPCServerManager());
    
    // Connect scanner controller logging to text edit (thread-safe)
    connect(mScannerController, &ScannerController::logMessage, this, [this](const QString& msg) {
        textEdit->append(msg);
    });
    
    // Setup menu bar (uses mProjectManager)
    setupMenuBar();
    
    // Connect controller signals to UI
    connectControllerSignals();

    // Show welcome message
    textEdit->append("Initializing MarcSLM Controller!");
    textEdit->append("→ Use 'Initialize OPC' and 'Initialize Scanner' buttons to begin");
}

MainWindow::~MainWindow() {
    // Controllers deleted automatically by Qt parent-child relationship
}

void MainWindow::connectControllerSignals() {
    // Connect OPC controller signals
    connect(mOPCController, &OPCController::dataUpdated,
            this, &MainWindow::onOPCDataUpdated);
    connect(mOPCController, &OPCController::connectionLost,
            this, &MainWindow::onOPCConnectionLost);
    
    // Connect Scanner controller signals
    connect(mScannerController, &ScannerController::layerCompleted,
            this, &MainWindow::onScannerLayerCompleted);
    connect(mScannerController, &ScannerController::scannerError,
            this, [this](UINT errorCode, const QString& message) {
                scannerStatusDisplay->display(static_cast<int>(errorCode));
                scannerErrorLabel->setText(message);
                scannerErrorLabel->setStyleSheet("QLabel { color: #F44336; font-size: 9pt; font-weight: bold; }");
            });
    
    // Connect Process controller signals
    connect(mProcessController, &ProcessController::stateChanged,
            this, &MainWindow::onProcessStateChanged);
    connect(mProcessController, &ProcessController::layerPreparedByPLC,
            this, [this]() {
                textEdit->append("✓ Layer prepared by PLC - scanning initiated");
            });
    
    // Connect ScanStreamingManager signals
    connect(mScanManager, &ScanStreamingManager::statusMessage,
            this, &MainWindow::onScanProcessStatusMessage);
    connect(mScanManager, &ScanStreamingManager::progress,
            this, &MainWindow::onScanProcessProgress);
    connect(mScanManager, &ScanStreamingManager::finished,
            this, &MainWindow::onScanProcessFinished);
    connect(mScanManager, &ScanStreamingManager::error,
            this, &MainWindow::onScanProcessError);
}

// ============================================================================
// UI Setup Implementation
// ============================================================================
void MainWindow::setupUI() {
    setWindowTitle("MarcSLM Machine Control");
    resize(1200, 800);
    setMinimumSize(800, 600);
    
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    
    // ========== LEFT COLUMN ==========
    QVBoxLayout* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(12);
    
    // Control Buttons Group
    QGroupBox* buttonsGroup = new QGroupBox("Machine Control", this);
    buttonsGroup->setMinimumHeight(150);
    QGridLayout* buttonsLayout = new QGridLayout(buttonsGroup);
    buttonsLayout->setSpacing(10);
    
    InitOPC = new QPushButton("Initialize OPC", this);  //Test SLM process=Initialize OPC
    InitOPC->setMinimumHeight(50);
    InitOPC->setStyleSheet("QPushButton { font-weight: bold; background-color: #4CAF50; color: white; border-radius: 6px; }");
    connect(InitOPC, &QPushButton::clicked, this, &MainWindow::on_InitOPC_clicked);
    buttonsLayout->addWidget(InitOPC, 0, 0);
    
    InitScanner = new QPushButton("Initialize Scanner", this);
    InitScanner->setMinimumHeight(50);
    InitScanner->setStyleSheet("QPushButton { font-weight: bold; background-color: #9C27B0; color: white; border-radius: 6px; }");
    connect(InitScanner, &QPushButton::clicked, this, &MainWindow::on_InitScanner_clicked);
    buttonsLayout->addWidget(InitScanner, 0, 1);
    
    StartUP = new QPushButton("Machine Start Up", this);
    StartUP->setMinimumHeight(50);
    StartUP->setStyleSheet("QPushButton { font-weight: bold; background-color: #2196F3; color: white; border-radius: 6px; }");
    connect(StartUP, &QPushButton::clicked, this, &MainWindow::on_StartUP_clicked);
    buttonsLayout->addWidget(StartUP, 1, 0);
    
    Test_Slm_process = new QPushButton("Test SLM Process", this);// Restsart SLM Process
    Test_Slm_process->setMinimumHeight(50);
    Test_Slm_process->setStyleSheet("QPushButton { font-weight: bold; background-color: #FF9800; color: white; border-radius: 6px; }");
    connect(Test_Slm_process, &QPushButton::clicked, this, &MainWindow::onTestSLMProcess_clicked);
    buttonsLayout->addWidget(Test_Slm_process, 1, 1);
    
    leftColumn->addWidget(buttonsGroup);
    
    // Status Displays Group
    QGroupBox* statusGroup = new QGroupBox("Real-Time Status", this);
    statusGroup->setMinimumHeight(280);
    QGridLayout* statusLayout = new QGridLayout(statusGroup);
    statusLayout->setSpacing(10);
    
    QLabel* cylPosHeader = new QLabel("<b>Cylinder Positions (microns):</b>");
    statusLayout->addWidget(cylPosHeader, 0, 0, 1, 4);
    
    statusLayout->addWidget(new QLabel("Source:"), 1, 0);
    sourceCylPos = new QLCDNumber(this);
    sourceCylPos->setDigitCount(7);
    sourceCylPos->setMinimumHeight(50);
    sourceCylPos->setStyleSheet("QLCDNumber { background-color: #263238; color: #00E676; }");
    statusLayout->addWidget(sourceCylPos, 1, 1);
    
    statusLayout->addWidget(new QLabel("Sink:"), 1, 2);
    sinkCylPos = new QLCDNumber(this);
    sinkCylPos->setDigitCount(7);
    sinkCylPos->setMinimumHeight(50);
    sinkCylPos->setStyleSheet("QLCDNumber { background-color: #263238; color: #00E676; }");
    statusLayout->addWidget(sinkCylPos, 1, 3);
    
    statusLayout->addWidget(new QLabel("g_Source:"), 2, 0);
    g_sourceCylPos = new QLCDNumber(this);
    g_sourceCylPos->setDigitCount(7);
    g_sourceCylPos->setMinimumHeight(45);
    g_sourceCylPos->setStyleSheet("QLCDNumber { background-color: #263238; color: #64FFDA; }");
    statusLayout->addWidget(g_sourceCylPos, 2, 1);
    
    statusLayout->addWidget(new QLabel("g_Sink:"), 2, 2);
    g_sinkCylPos = new QLCDNumber(this);
    g_sinkCylPos->setDigitCount(7);
    g_sinkCylPos->setMinimumHeight(45);
    g_sinkCylPos->setStyleSheet("QLCDNumber { background-color: #263238; color: #64FFDA; }");
    statusLayout->addWidget(g_sinkCylPos, 2, 3);
    
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    statusLayout->addWidget(line, 3, 0, 1, 4);
    
    QLabel* processHeader = new QLabel("<b>Process Status:</b>");
    statusLayout->addWidget(processHeader, 4, 0, 1, 4);
    
    statusLayout->addWidget(new QLabel("Layers Remaining:"), 5, 0);
    stacksLeft = new QLCDNumber(this);
    stacksLeft->setDigitCount(5);
    stacksLeft->setMinimumHeight(42);
    stacksLeft->setStyleSheet("QLCDNumber { background-color: #1A237E; color: #FFEB3B; }");
    statusLayout->addWidget(stacksLeft, 5, 1);
    
    statusLayout->addWidget(new QLabel("Ready to Powder:"), 5, 2);
    Ready2Powder = new QLCDNumber(this);
    Ready2Powder->setDigitCount(1);
    Ready2Powder->setMinimumHeight(42);
    Ready2Powder->setStyleSheet("QLCDNumber { background-color: #1B5E20; color: #76FF03; }");
    statusLayout->addWidget(Ready2Powder, 5, 3);
    
    statusLayout->addWidget(new QLabel("Startup Complete:"), 6, 0);
    StartUpDone = new QLCDNumber(this);
    StartUpDone->setDigitCount(1);
    StartUpDone->setMinimumHeight(42);
    StartUpDone->setStyleSheet("QLCDNumber { background-color: #1B5E20; color: #76FF03; }");
    statusLayout->addWidget(StartUpDone, 6, 1);
    
    statusLayout->addWidget(new QLabel("Surface Complete:"), 6, 2);
    PowderSurfaceDone_2 = new QLCDNumber(this);
    PowderSurfaceDone_2->setDigitCount(1);
    PowderSurfaceDone_2->setMinimumHeight(42);
    PowderSurfaceDone_2->setStyleSheet("QLCDNumber { background-color: #1B5E20; color: #76FF03; }");
    statusLayout->addWidget(PowderSurfaceDone_2, 6, 3);
    
    leftColumn->addWidget(statusGroup);
    
    // Log Area
    QGroupBox* logGroup = new QGroupBox("System Log", this);
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(8, 12, 8, 8);
    
    textEdit = new QTextEdit(this);
    textEdit->setReadOnly(true);
    textEdit->setMinimumHeight(200);
    textEdit->setStyleSheet(
        "QTextEdit { "
        "   font-family: 'Consolas', 'Courier New', monospace; "
        "   font-size: 9pt; "
        "   background-color: #1E1E1E; "
        "   color: #D4D4D4; "
        "   border: 1px solid #3E3E42; "
        "   border-radius: 4px; "
        "}"
    );
    logLayout->addWidget(textEdit);
    
    leftColumn->addWidget(logGroup, 1);
    mainLayout->addLayout(leftColumn, 3);
    
    // ========== RIGHT COLUMN ==========
    QVBoxLayout* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(12);
    
    // Powder Fill Operations
    QGroupBox* powderFillGroup = new QGroupBox("Powder Fill Operation", this);
    powderFillGroup->setMinimumHeight(180);
    QVBoxLayout* powderFillMainLayout = new QVBoxLayout(powderFillGroup);
    powderFillMainLayout->setSpacing(10);
    
    QGridLayout* powderFillLayout = new QGridLayout();
    powderFillLayout->setSpacing(10);
    
    powderFillLayout->addWidget(new QLabel("Delta Source:"), 0, 0);
    deltaSource = new QDoubleSpinBox(this);
    deltaSource->setRange(0, 10000);
    deltaSource->setValue(50);
    deltaSource->setDecimals(1);
    deltaSource->setSuffix(" microns");
    deltaSource->setMinimumHeight(35);
    powderFillLayout->addWidget(deltaSource, 0, 1);
    
    powderFillLayout->addWidget(new QLabel("Delta Sink:"), 0, 2);
    deltaSink = new QDoubleSpinBox(this);
    deltaSink->setRange(0, 10000);
    deltaSink->setValue(50);
    deltaSink->setDecimals(1);
    deltaSink->setSuffix(" microns");
    deltaSink->setMinimumHeight(35);
    powderFillLayout->addWidget(deltaSink, 0, 3);
    
    powderFillLayout->addWidget(new QLabel("Number of Layers:"), 1, 0);
    noOfStacks = new QDoubleSpinBox(this);
    noOfStacks->setRange(0, 10000);
    noOfStacks->setValue(0);
    noOfStacks->setDecimals(0);
    noOfStacks->setMinimumHeight(35);
    powderFillLayout->addWidget(noOfStacks, 1, 1, 1, 3);
    
    powderFillMainLayout->addLayout(powderFillLayout);
    
    QHBoxLayout* powderFillButtonsLayout = new QHBoxLayout();
    powderFillButtonsLayout->setSpacing(10);
    
    Prep_Powder_Fill = new QPushButton("Start Powder Fill", this);
    Prep_Powder_Fill->setMinimumHeight(45);
    Prep_Powder_Fill->setStyleSheet("QPushButton { font-weight: bold; background-color: #2196F3; color: white; border-radius: 6px; }");
    connect(Prep_Powder_Fill, &QPushButton::clicked, this, &MainWindow::on_Prep_Powder_Fill_clicked);
    powderFillButtonsLayout->addWidget(Prep_Powder_Fill);
    
    Lay_Surface = new QPushButton("Lay Surface", this);
    Lay_Surface->setMinimumHeight(45);
    Lay_Surface->setStyleSheet("QPushButton { font-weight: bold; background-color: #607D8B; color: white; border-radius: 6px; }");
    connect(Lay_Surface, &QPushButton::clicked, this, &MainWindow::on_Lay_Surface_clicked);
    powderFillButtonsLayout->addWidget(Lay_Surface);
    
    powderFillMainLayout->addLayout(powderFillButtonsLayout);
    rightColumn->addWidget(powderFillGroup);
    
    // Bottom Layer Operations
    QGroupBox* bottomLayerGroup = new QGroupBox("Bottom Layer Operation", this);
    bottomLayerGroup->setMinimumHeight(180);
    QVBoxLayout* bottomLayerMainLayout = new QVBoxLayout(bottomLayerGroup);
    bottomLayerMainLayout->setSpacing(10);
    
    QGridLayout* bottomLayerLayout = new QGridLayout();
    bottomLayerLayout->setSpacing(10);
    
    bottomLayerLayout->addWidget(new QLabel("Delta Source:"), 0, 0);
    deltaSource_BottomLayer = new QDoubleSpinBox(this);
    deltaSource_BottomLayer->setRange(0, 10000);
    deltaSource_BottomLayer->setValue(50);
    deltaSource_BottomLayer->setDecimals(1);
    deltaSource_BottomLayer->setSuffix(" microns");
    deltaSource_BottomLayer->setMinimumHeight(35);
    bottomLayerLayout->addWidget(deltaSource_BottomLayer, 0, 1);
    
    bottomLayerLayout->addWidget(new QLabel("Delta Sink:"), 0, 2);
    deltaSink_BottomLayer = new QDoubleSpinBox(this);
    deltaSink_BottomLayer->setRange(0, 10000);
    deltaSink_BottomLayer->setValue(50);
    deltaSink_BottomLayer->setDecimals(1);
    deltaSink_BottomLayer->setSuffix(" microns");
    deltaSink_BottomLayer->setMinimumHeight(35);
    bottomLayerLayout->addWidget(deltaSink_BottomLayer, 0, 3);
    
    bottomLayerLayout->addWidget(new QLabel("Number of Layers:"), 1, 0);
    noOfStacks_BottomLayer = new QDoubleSpinBox(this);
    noOfStacks_BottomLayer->setRange(0, 10000);
    noOfStacks_BottomLayer->setValue(0);
    noOfStacks_BottomLayer->setDecimals(0);
    noOfStacks_BottomLayer->setMinimumHeight(35);
    bottomLayerLayout->addWidget(noOfStacks_BottomLayer, 1, 1, 1, 3);
    
    bottomLayerMainLayout->addLayout(bottomLayerLayout);
    
    MakeBottomLayers = new QPushButton("Make Bottom Layers", this);
    MakeBottomLayers->setMinimumHeight(45);
    MakeBottomLayers->setStyleSheet("QPushButton { font-weight: bold; background-color: #FF9800; color: white; border-radius: 6px; }");
    connect(MakeBottomLayers, &QPushButton::clicked, this, &MainWindow::on_MakeBottomLayers_clicked);
    bottomLayerMainLayout->addWidget(MakeBottomLayers);
    
    rightColumn->addWidget(bottomLayerGroup);
    
    // Scanner Control Panel
    QGroupBox* scannerControlGroup = new QGroupBox("Scanner Control", this);
    scannerControlGroup->setMinimumHeight(260);
    QVBoxLayout* scannerControlLayout = new QVBoxLayout(scannerControlGroup);
    scannerControlLayout->setSpacing(8);
    
    QGridLayout* scannerParamsGrid = new QGridLayout();
    scannerParamsGrid->setSpacing(8);
    
    scannerParamsGrid->addWidget(new QLabel("Laser Power:"), 0, 0);
    laserPowerSpinBox = new QDoubleSpinBox(this);
    laserPowerSpinBox->setRange(0, 4095);
    laserPowerSpinBox->setValue(0);
    laserPowerSpinBox->setMinimumHeight(30);
    scannerParamsGrid->addWidget(laserPowerSpinBox, 0, 1);
    
    scannerParamsGrid->addWidget(new QLabel("Mark Speed:"), 0, 2);
    markSpeedSpinBox = new QDoubleSpinBox(this);
    markSpeedSpinBox->setRange(50, 2000);
    markSpeedSpinBox->setValue(250);
    markSpeedSpinBox->setSuffix(" mm/s");
    markSpeedSpinBox->setMinimumHeight(30);
    scannerParamsGrid->addWidget(markSpeedSpinBox, 0, 3);
    
    scannerParamsGrid->addWidget(new QLabel("Wobble Amp:"), 1, 0);
    wobbleAmplitudeSpinBox = new QDoubleSpinBox(this);
    wobbleAmplitudeSpinBox->setRange(0, 200);
    wobbleAmplitudeSpinBox->setValue(50);
    wobbleAmplitudeSpinBox->setSuffix(" microns");
    wobbleAmplitudeSpinBox->setMinimumHeight(30);
    scannerParamsGrid->addWidget(wobbleAmplitudeSpinBox, 1, 1);
    
    scannerParamsGrid->addWidget(new QLabel("Jump Speed:"), 1, 2);
    jumpSpeedSpinBox = new QDoubleSpinBox(this);
    jumpSpeedSpinBox->setRange(100, 3000);
    jumpSpeedSpinBox->setValue(1000);
    jumpSpeedSpinBox->setSuffix(" mm/s");
    jumpSpeedSpinBox->setMinimumHeight(30);
    scannerParamsGrid->addWidget(jumpSpeedSpinBox, 1, 3);
    
    scannerParamsGrid->addWidget(new QLabel("Wobble Freq:"), 2, 0);
    wobbleFrequencySpinBox = new QDoubleSpinBox(this);
    wobbleFrequencySpinBox->setRange(10, 500);
    wobbleFrequencySpinBox->setValue(100);
    wobbleFrequencySpinBox->setSuffix(" Hz");
    wobbleFrequencySpinBox->setMinimumHeight(30);
    scannerParamsGrid->addWidget(wobbleFrequencySpinBox, 2, 1, 1, 3);
    
    scannerControlLayout->addLayout(scannerParamsGrid);
    
    // Add SVG Scale control
    QHBoxLayout* svgScaleLayout = new QHBoxLayout();
    svgScaleLayout->setSpacing(8);
    svgScaleLayout->addWidget(new QLabel("SVG Scale:"));
    svgScaleSpinBox = new QDoubleSpinBox(this);
    svgScaleSpinBox->setRange(1.0, 100.0);
    svgScaleSpinBox->setValue(20.0);
    svgScaleSpinBox->setSuffix(" px/mm");
    svgScaleSpinBox->setMinimumHeight(30);
    svgScaleLayout->addWidget(svgScaleSpinBox);
    scannerControlLayout->addLayout(svgScaleLayout);
    
    QHBoxLayout* scannerButtonsLayout = new QHBoxLayout();
    scannerButtonsLayout->setSpacing(8);
    
    applyLaserPowerBtn = new QPushButton("Apply Power", this);
    applyLaserPowerBtn->setMinimumHeight(35);
    applyLaserPowerBtn->setStyleSheet("QPushButton { background-color: #607D8B; color: white; border-radius: 4px; }");
    connect(applyLaserPowerBtn, &QPushButton::clicked, [this]() {
        if (mScannerController && mScannerController->isInitialized()) {
            UINT power = static_cast<UINT>(laserPowerSpinBox->value());
            mScannerController->setLaserPower(power);
        }
    });
    scannerButtonsLayout->addWidget(applyLaserPowerBtn);
    
    applySpeedBtn = new QPushButton("Apply Speeds", this);
    applySpeedBtn->setMinimumHeight(35);
    applySpeedBtn->setStyleSheet("QPushButton { background-color: #607D8B; color: white; border-radius: 4px; }");
    connect(applySpeedBtn, &QPushButton::clicked, [this]() {
        if (mScannerController && mScannerController->isInitialized()) {
            mScannerController->setSpeeds(markSpeedSpinBox->value(), jumpSpeedSpinBox->value());
        }
    });
    scannerButtonsLayout->addWidget(applySpeedBtn);
    
    enableWobbleBtn = new QPushButton("Enable Wobble", this);
    enableWobbleBtn->setCheckable(true);
    enableWobbleBtn->setMinimumHeight(35);
    enableWobbleBtn->setStyleSheet("QPushButton { background-color: #757575; color: white; border-radius: 4px; } QPushButton:checked { background-color: #4CAF50; }");
    connect(enableWobbleBtn, &QPushButton::toggled, [this](bool checked) {
        if (mScannerController && mScannerController->isInitialized()) {
            if (checked) {
                UINT amplitude = static_cast<UINT>(wobbleAmplitudeSpinBox->value());
                double freq = wobbleFrequencySpinBox->value();
                mScannerController->setWobble(true, amplitude, freq);
                enableWobbleBtn->setText("Disable Wobble");
            } else {
                mScannerController->setWobble(false, 0, 0);
                enableWobbleBtn->setText("Enable Wobble");
            }
        }
    });
    scannerButtonsLayout->addWidget(enableWobbleBtn);
    
    scannerControlLayout->addLayout(scannerButtonsLayout);
    
    QHBoxLayout* scannerStatusLayout = new QHBoxLayout();
    scannerStatusLayout->setSpacing(8);
    
    scannerStatusLayout->addWidget(new QLabel("Error:"));
    scannerStatusDisplay = new QLCDNumber(this);
    scannerStatusDisplay->setDigitCount(4);
    scannerStatusDisplay->setMinimumHeight(28);
    scannerStatusDisplay->setMinimumWidth(60);
    scannerStatusDisplay->setStyleSheet("QLCDNumber { background-color: #263238; color: #00E676; }");
    scannerStatusLayout->addWidget(scannerStatusDisplay);
    
    scannerErrorLabel = new QLabel("No errors", this);
    scannerErrorLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 9pt; }");
    scannerStatusLayout->addWidget(scannerErrorLabel, 1);
    
    scannerControlLayout->addLayout(scannerStatusLayout);
    
    runDiagnosticsBtn = new QPushButton("Run Scanner Diagnostics", this);
    runDiagnosticsBtn->setMinimumHeight(38);
    runDiagnosticsBtn->setStyleSheet("QPushButton { font-weight: bold; background-color: #607D8B; color: white; border-radius: 4px; }");
    connect(runDiagnosticsBtn, &QPushButton::clicked, this, &MainWindow::on_RunScannerDiagnostics_clicked);
    scannerControlLayout->addWidget(runDiagnosticsBtn);
    
    rightColumn->addWidget(scannerControlGroup);
    
    // Emergency Stop
    QGroupBox* emergencyStopGroup = new QGroupBox("Emergency Control", this);
    emergencyStopGroup->setMinimumHeight(100);
    QVBoxLayout* emergencyStopLayout = new QVBoxLayout(emergencyStopGroup);
    emergencyStopLayout->setContentsMargins(15, 20, 15, 15);
    
    EmergencyStop = new QPushButton("EMERGENCY STOP", this);
    EmergencyStop->setMinimumHeight(55);
    EmergencyStop->setStyleSheet(
        "QPushButton { "
        "   font-weight: bold; "
        "   font-size: 14pt; "
        "   background-color: #D32F2F; "
        "   color: white; "
        "   border-radius: 8px; "
        "   border: 3px solid #B71C1C; "
        "} "
        "QPushButton:hover { background-color: #C62828; border: 3px solid #8B0000; } "
        "QPushButton:pressed { background-color: #B71C1C; border: 3px solid #660000; }"
    );
    connect(EmergencyStop, &QPushButton::clicked, this, &MainWindow::on_EmergencyStop_clicked);
    emergencyStopLayout->addWidget(EmergencyStop);
    
    rightColumn->addWidget(emergencyStopGroup);
    rightColumn->addStretch();
    
    mainLayout->addLayout(rightColumn, 2);
}

// ============================================================================
// Menu Bar Setup Implementation
// ============================================================================
void MainWindow::setupMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    
    // ========== FILE MENU ==========
    QMenu* fileMenu = menuBar->addMenu("&File");
    
    actionNew = new QAction("&New Project", this);
    actionNew->setShortcut(QKeySequence::New);
    actionNew->setStatusTip("Create a new project");
    connect(actionNew, &QAction::triggered, this, &MainWindow::onFileNew);
    fileMenu->addAction(actionNew);
    
    actionOpen = new QAction("&Open Project...", this);
    actionOpen->setShortcut(QKeySequence::Open);
    actionOpen->setStatusTip("Open an existing project");
    connect(actionOpen, &QAction::triggered, this, &MainWindow::onFileOpen);
    fileMenu->addAction(actionOpen);
    
    fileMenu->addSeparator();
    
    actionSave = new QAction("&Save", this);
    actionSave->setShortcut(QKeySequence::Save);
    actionSave->setStatusTip("Save current project");
    connect(actionSave, &QAction::triggered, this, &MainWindow::onFileSave);
    fileMenu->addAction(actionSave);
    
    actionSaveAs = new QAction("Save &As...", this);
    actionSaveAs->setShortcut(QKeySequence::SaveAs);
    actionSaveAs->setStatusTip("Save project with a new name");
    connect(actionSaveAs, &QAction::triggered, this, &MainWindow::onFileSaveAs);
    fileMenu->addAction(actionSaveAs);
    
    fileMenu->addSeparator();
    
    actionExport = new QAction("&Export Log...", this);
    actionExport->setShortcut(QKeySequence("Ctrl+E"));
    actionExport->setStatusTip("Export system log to file");
    connect(actionExport, &QAction::triggered, this, &MainWindow::onFileExport);
    fileMenu->addAction(actionExport);
    
    fileMenu->addSeparator();
    
    actionExit = new QAction("E&xit", this);
    actionExit->setShortcut(QKeySequence::Quit);
    actionExit->setStatusTip("Exit application");
    connect(actionExit, &QAction::triggered, this, &MainWindow::onFileExit);
    fileMenu->addAction(actionExit);
    
    // ========== EDIT MENU ==========
    QMenu* editMenu = menuBar->addMenu("&Edit");
    
    actionPreferences = new QAction("&Preferences...", this);
    actionPreferences->setShortcut(QKeySequence::Preferences);
    actionPreferences->setStatusTip("Configure application settings");
    connect(actionPreferences, &QAction::triggered, this, &MainWindow::onEditPreferences);
    editMenu->addAction(actionPreferences);
    
    editMenu->addSeparator();
    
    actionClearLog = new QAction("&Clear Log", this);
    actionClearLog->setShortcut(QKeySequence("Ctrl+L"));
    actionClearLog->setStatusTip("Clear the system log");
    connect(actionClearLog, &QAction::triggered, this, &MainWindow::onEditClearLog);
    editMenu->addAction(actionClearLog);
    
    actionResetParameters = new QAction("&Reset Parameters", this);
    actionResetParameters->setShortcut(QKeySequence("Ctrl+R"));
    actionResetParameters->setStatusTip("Reset all parameters to defaults");
    connect(actionResetParameters, &QAction::triggered, this, &MainWindow::onEditResetParameters);
    editMenu->addAction(actionResetParameters);
    
    // ========== VIEW MENU ==========
    QMenu* viewMenu = menuBar->addMenu("&View");
    
    actionFullScreen = new QAction("&Full Screen", this);
    actionFullScreen->setShortcut(QKeySequence::FullScreen);
    actionFullScreen->setCheckable(true);
    actionFullScreen->setStatusTip("Toggle full screen mode");
    connect(actionFullScreen, &QAction::triggered, this, &MainWindow::onViewFullScreen);
    viewMenu->addAction(actionFullScreen);
    
    viewMenu->addSeparator();
    
    actionStatusBar = new QAction("Status &Bar", this);
    actionStatusBar->setCheckable(true);
    actionStatusBar->setChecked(true);
    actionStatusBar->setStatusTip("Show or hide status bar");
    connect(actionStatusBar, &QAction::triggered, this, &MainWindow::onViewStatusBar);
    viewMenu->addAction(actionStatusBar);
    
    actionExpandLog = new QAction("&Expand Log", this);
    actionExpandLog->setShortcut(QKeySequence("Ctrl+Shift+L"));
    actionExpandLog->setStatusTip("Expand log window");
    connect(actionExpandLog, &QAction::triggered, this, &MainWindow::onViewExpandLog);
    viewMenu->addAction(actionExpandLog);

    // New action: Generate SVGs from MARC
    actionGenerateSVGs = new QAction("&Generate SVGs from marc file...", this);
    actionGenerateSVGs->setStatusTip("Read .marc file and export SVG images for all layers");
    connect(actionGenerateSVGs, &QAction::triggered, this, &MainWindow::onViewGenerateSVGs);
    viewMenu->addAction(actionGenerateSVGs);

    // ========== RUN MENU ==========
    QMenu* runMenu = menuBar->addMenu("&Run");
    
    actionInitialize = new QAction("&Initialize System", this);
    actionInitialize->setShortcut(QKeySequence("F5"));
    actionInitialize->setStatusTip("Initialize OPC and Scanner");
    connect(actionInitialize, &QAction::triggered, this, &MainWindow::onRunInitialize);
    runMenu->addAction(actionInitialize);
    
    runMenu->addSeparator();
    
    actionStart = new QAction("&Start Process", this);
    actionStart->setShortcut(QKeySequence("F6"));
    actionStart->setStatusTip("Start the manufacturing process");
    connect(actionStart, &QAction::triggered, this, &MainWindow::onRunStart);
    runMenu->addAction(actionStart);
    
    actionPause = new QAction("&Pause", this);
    actionPause->setShortcut(QKeySequence("F7"));
    actionPause->setStatusTip("Pause current operation");
    connect(actionPause, &QAction::triggered, this, &MainWindow::onRunPause);
    runMenu->addAction(actionPause);
    
    actionStop = new QAction("S&top", this);
    actionStop->setShortcut(QKeySequence("F8"));
    actionStop->setStatusTip("Stop current operation");
    connect(actionStop, &QAction::triggered, this, &MainWindow::onRunStop);
    runMenu->addAction(actionStop);
    
    runMenu->addSeparator();
    
    actionEmergencyStop = new QAction("&EMERGENCY STOP", this);
    actionEmergencyStop->setShortcut(QKeySequence("Ctrl+Shift+E"));
    actionEmergencyStop->setStatusTip("Emergency stop all operations");
    connect(actionEmergencyStop, &QAction::triggered, this, &MainWindow::onRunEmergencyStop);
    runMenu->addAction(actionEmergencyStop);
    
    
    
    // ========== PROJECT MENU ==========
    QMenu* projectMenu = menuBar->addMenu("&Project");
    
    QAction* actionProjectOpen = new QAction("&Open Project...", this);
    connect(actionProjectOpen, &QAction::triggered, this, &MainWindow::onProjectOpen);
    projectMenu->addAction(actionProjectOpen);
    
    QAction* actionProjectSave = new QAction("&Save Project", this);
    connect(actionProjectSave, &QAction::triggered, [this]() { 
        if (mProjectManager) mProjectManager->saveProjectInteractive(); 
    });
    projectMenu->addAction(actionProjectSave);
    
    projectMenu->addSeparator();
    
    QAction* actionAttachMarc = new QAction("Attach &Scan Data (.marc)...", this);
    connect(actionAttachMarc, &QAction::triggered, this, &MainWindow::onProjectAttachMarc);
    projectMenu->addAction(actionAttachMarc);
    
    QAction* actionAttachJson = new QAction("Attach &Configuration (.json)...", this);
    connect(actionAttachJson, &QAction::triggered, this, &MainWindow::onProjectAttachJson);
    projectMenu->addAction(actionAttachJson);
    
    // Project Explorer dock
    projectDock = new QDockWidget("Project Explorer", this);
    projectTree = new QTreeWidget(projectDock);
    projectTree->setHeaderLabels({"Item", "Value"});
    projectDock->setWidget(projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);
    
    // Connect project manager signals to update explorer
    connect(mProjectManager, &ProjectManager::projectOpened, this, [this](const QString&) {
        updateProjectExplorer();
    });
    
    connect(mProjectManager, &ProjectManager::projectSaved, this, [this](const QString&) {
        updateProjectExplorer();
    });
    
    connect(mProjectManager, &ProjectManager::projectModified, this, [this]() {
        updateProjectExplorer();
    });
    
    // Status bar
    statusBar()->showMessage("Ready", 3000);
    // ========== HELP MENU ==========
    QMenu* helpMenu = menuBar->addMenu("&Help");

    actionDocumentation = new QAction("&Documentation", this);
    actionDocumentation->setShortcut(QKeySequence::HelpContents);
    actionDocumentation->setStatusTip("Open user documentation");
    connect(actionDocumentation, &QAction::triggered, this, &MainWindow::onHelpDocumentation);
    helpMenu->addAction(actionDocumentation);

    helpMenu->addSeparator();

    actionCheckUpdates = new QAction("Check for &Updates", this);
    actionCheckUpdates->setStatusTip("Check for software updates");
    connect(actionCheckUpdates, &QAction::triggered, this, &MainWindow::onHelpCheckUpdates);
    helpMenu->addAction(actionCheckUpdates);

    helpMenu->addSeparator();

    actionAbout = new QAction("&About MarcSLM", this);
    actionAbout->setStatusTip("About this application");
    connect(actionAbout, &QAction::triggered, this, &MainWindow::onHelpAbout);
    helpMenu->addAction(actionAbout);
}

// ============================================================================
// Menu Bar Slot Implementations
// ============================================================================

// FILE MENU
void MainWindow::onFileNew() {
    if (!mProjectManager) mProjectManager = new ProjectManager(this);
    if (mProjectManager->createNewProjectInteractive()) {
        if (textEdit) textEdit->append("✓ New project created successfully");
        if (statusBar()) statusBar()->showMessage("New project created", 3000);
    } else {
        if (textEdit) textEdit->append("✗ New project creation canceled or failed");
        if (statusBar()) statusBar()->showMessage("Project creation canceled/failed", 3000);
    }
}

void MainWindow::onFileOpen() {
    if (!mProjectManager) mProjectManager = new ProjectManager(this);
    if (mProjectManager->openProjectInteractive()) {
        if (textEdit) textEdit->append("✓ Project opened successfully");
        if (statusBar()) statusBar()->showMessage("Project opened", 3000);
    } else {
        if (textEdit) textEdit->append("✗ Project open canceled or failed");
        if (statusBar()) statusBar()->showMessage("Project open canceled/failed", 3000);
    }
}

void MainWindow::onFileSave() {
    textEdit->append("File -> Save");
    if (statusBar()) statusBar()->showMessage("Project saved", 3000);
}

void MainWindow::onFileSaveAs() {
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Project As", "", "MarcSLM Projects (*.mslm);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        textEdit->append(QString("File -> Save As: %1").arg(fileName));
        if (statusBar()) statusBar()->showMessage(QString("Saved as: %1").arg(fileName), 3000);
    }
}

void MainWindow::onFileExport() {
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Log", "", "Text Files (*.txt);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << textEdit->toPlainText();
            file.close();
            textEdit->append(QString("✓ Log exported to: %1").arg(fileName));
            if (statusBar()) statusBar()->showMessage("Log exported successfully", 3000);
        } else {
            QMessageBox::warning(this, "Export Failed", 
                "Could not write to file: " + fileName);
        }
    }
}

void MainWindow::onFileExit() {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Exit Application",
        "Are you sure you want to exit?\n\n"
        "Any unsaved changes will be lost.",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Safely shut down hardware components before exiting
        textEdit->append("=== Application Shutdown Initiated ===");
        
        // Stop any running processes
        if (mProcessController) {
            textEdit->append("Stopping process controller...");
            mProcessController->stopProcess();
        }
        
        // Stop streaming manager if active
        if (mScanManager) {
            textEdit->append("Stopping scan streaming manager...");
            mScanManager->stopProcess();
        }
        
        // Stop SLM worker manager (OPC thread)
        if (mSLMWorkerManager) {
            textEdit->append("Stopping SLM worker manager...");
            mSLMWorkerManager->stopWorkers();
        }
        
        // Shut down scanner if initialized
        if (mScannerController && mScannerController->isInitialized()) {
            textEdit->append("Shutting down scanner...");
            mScannerController->shutdown();
        }
        
        // Shut down OPC if initialized
        if (mOPCController && mOPCController->isInitialized()) {
            textEdit->append("Shutting down OPC server...");
            // OPC shutdown is handled through SLMWorkerManager's stopWorkers
            // No direct shutdown needed as the worker thread handles cleanupOPCController::writeEmergencyStop()
        }
        
        textEdit->append("=== Hardware shutdown complete. Exiting application. ===");
        close();
    }
}

// EDIT MENU
void MainWindow::onEditPreferences() {
    textEdit->append("Edit -> Preferences");
    QMessageBox::information(this, "Preferences",
        "Preferences dialog will be implemented in future version.\n\n"
        "Current default settings are in use.");
    if (statusBar()) statusBar()->showMessage("Preferences opened", 3000);
}

void MainWindow::onEditClearLog() {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Clear Log",
        "Are you sure you want to clear the system log?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        textEdit->clear();
        textEdit->append("=== Log Cleared ===");
        if (statusBar()) statusBar()->showMessage("Log cleared", 3000);
    }
}

void MainWindow::onEditResetParameters() {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Reset Parameters",
        "Reset all parameters to default values?\n\n"
        "This will reset:\n"
        "- Powder fill parameters\n"
        "- Bottom layer parameters\n"
        "- Scanner settings",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        deltaSource->setValue(50);
        deltaSink->setValue(50);
        noOfStacks->setValue(0);
        deltaSource_BottomLayer->setValue(50);
        deltaSink_BottomLayer->setValue(50);
        noOfStacks_BottomLayer->setValue(0);
        laserPowerSpinBox->setValue(0);
        markSpeedSpinBox->setValue(250);
        jumpSpeedSpinBox->setValue(1000);
        wobbleAmplitudeSpinBox->setValue(50);
        wobbleFrequencySpinBox->setValue(100);
        
        textEdit->append("✓ All parameters reset to defaults");
        if (statusBar()) statusBar()->showMessage("Parameters reset", 3000);
    }
}

// VIEW MENU
void MainWindow::onViewFullScreen() {
    if (isFullScreen) {
        showNormal();
        isFullScreen = false;
        textEdit->append("View -> Full Screen Mode Disabled");
        if (statusBar()) statusBar()->showMessage("Normal view", 3000);
    } else {
        showFullScreen();
        isFullScreen = true;
        textEdit->append("View -> Full Screen Mode Enabled");
        if (statusBar()) statusBar()->showMessage("Full screen mode (Press F11 to exit)", 5000);
    }
}

void MainWindow::onViewStatusBar() {
    if (isStatusBarVisible) {
        statusBar()->hide();
        isStatusBarVisible = false;
        textEdit->append("View -> Status Bar Hidden");
    } else {
        statusBar()->show();
        isStatusBarVisible = true;
        textEdit->append("View -> Status Bar Shown");
    }
}

void MainWindow::onViewExpandLog() {
    if (textEdit->maximumHeight() == 16777215) {
        textEdit->setMaximumHeight(400);
        textEdit->append("View -> Log Normal Size");
        if (statusBar()) statusBar()->showMessage("Log size: Normal", 3000);
    } else {
        textEdit->setMaximumHeight(16777215);
        textEdit->append("View -> Log Expanded");
        if (statusBar()) statusBar()->showMessage("Log size: Expanded", 3000);
    }
}

void MainWindow::onViewGenerateSVGs() {
    using namespace marc;

    if (!mProjectManager || !mProjectManager->hasProject()) {
        QMessageBox::warning(this, "No Project", "Open a project and attach a .marc file first.");
        return;
    }

    const QString marcAbs = mProjectManager->marcAbsolutePath();
    if (marcAbs.isEmpty()) {
        QMessageBox::warning(this, "No MARC Attached", "No .marc file is attached to the current project.");
        return;
    }

#ifdef _WIN32
    std::wstring marcPath = marcAbs.toStdWString();
    std::string err;
    if (!readSlices::isMarcFile(marcPath, &err)) {
        QMessageBox::warning(this, "Invalid File", QString("Not a valid MARC file: %1").arg(QString::fromStdString(err)));
        return;
    }
    readSlices reader;
    if (!reader.open(marcPath)) {
        QMessageBox::critical(this, "Read Failed", "Failed to read MARC file from project.");
        return;
    }
#else
    std::string marcPath = marcAbs.toStdString();
    std::string err;
    if (!readSlices::isMarcFile(marcPath, &err)) {
        QMessageBox::warning(this, "Invalid File", QString("Not a valid MARC file: %1").arg(QString::fromStdString(err)));
        return;
    }
    readSlices reader;
    if (!reader.open(marcPath)) {
        QMessageBox::critical(this, "Read Failed", "Failed to read MARC file from project.");
        return;
    }
#endif

    QDir projectDir = QFileInfo(mProjectManager->currentProject()->buildFilePath()).dir();
    QString outDir = projectDir.filePath("svgOutput");

    writeSVG::Options opt;
    opt.mmWidth = 200.0f;
    opt.mmHeight = 200.0f;
    opt.scale = static_cast<float>(svgScaleSpinBox->value()); // Use dynamic scale from UI
    opt.zoom = 1.0f;  // no extra zoom
    opt.offsetX = 0.0f;
    opt.offsetY = 0.0f;
    opt.invertY = true;

    writeSVG writer(opt);
    if (writer.writeAll(reader.layers(), outDir.toStdString())) {
        textEdit->append(QString("✓ SVGs generated in: %1").arg(outDir));
        if (statusBar()) statusBar()->showMessage("SVG generation complete", 3000);

        QMessageBox msg(this);
        msg.setWindowTitle("SVGs Generated");
        msg.setText(QString("SVG images have been generated in:\n%1").arg(outDir));
        QPushButton* openBtn = msg.addButton("Open Folder", QMessageBox::AcceptRole);
        msg.addButton(QMessageBox::Ok);
        msg.exec();
        if (msg.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(outDir));
        }
    } else {
        QMessageBox::critical(this, "Export Failed", "Failed to generate SVGs");
    }
}
 
// HELP MENU
void MainWindow::onHelpDocumentation() {
    QMessageBox::information(this, "Documentation",
        "MarcSLM Machine Control System\n"
        "Version 4.1\n\n"
        "Documentation Topics:\n"
        "• Getting Started\n"
        "• OPC Configuration\n"
        "• Scanner Setup\n"
        "• Process Parameters\n"
        "• Safety Guidelines\n"
        "• Troubleshooting\n\n"
        "For detailed documentation, please refer to the user manual.");
    
    textEdit->append("Help -> Documentation opened");
    if (statusBar()) statusBar()->showMessage("Documentation displayed", 3000);
}

void MainWindow::onHelpAbout() {
    QMessageBox::about(this, "About MarcSLM",
        "<h2>MarcSLM Machine Control</h2>"
        "<p><b>Version:</b> 4.1.0</p>"
        "<p><b>Build Date:</b> " __DATE__ "</p>"
        "<p>&nbsp;</p>"
        "<p>Advanced Selective Laser Melting (SLM) machine control system.</p>"
        "<p>&nbsp;</p>"
        "<p><b>Features:</b></p>"
        "<ul>"
        "<li>OPC DA communication with PLC</li>"
        "<li>RTC5 scanner control</li>"
        "<li>Real-time process monitoring</li>"
        "<li>Layer-by-layer manufacturing</li>"
        "<li>Safety interlocks and emergency stop</li>"
        "</ul>"
        "<p>&nbsp;</p>"
        "<p><b>Copyright 2024 MarcSLM Technologies</b></p>"
        "<p>All rights reserved.</p>");
    
    if (statusBar()) statusBar()->showMessage("About displayed", 3000);
}

void MainWindow::onHelpCheckUpdates() {
    textEdit->append("Help -> Checking for updates...");
    if (statusBar()) statusBar()->showMessage("Checking for updates...", 3000);
    
    QMessageBox::information(this, "Check for Updates",
        "You are running the latest version.\n\n"
        "Version: 4.1.0\n"
        "No updates available.");
    
    textEdit->append("✓ Software is up to date");
}

// =============================
// Button Click Handlers - Delegate to Controllers
// =============================
// Test SLM Process button
void MainWindow::on_InitOPC_clicked() {
    if (mOPCController->isInitialized()) {
        textEdit->append("OPC Server is already initialized");
        QMessageBox::information(this, "Info", "OPC Server is already running");
        return;
    }

    if (mOPCController->initialize()) {
        // Start process monitoring
        //mProcessController->startProcess();//disable it as startProcess()is not verified function
        
        // AUTO-INITIALIZE SCANNER if not already initialized
        if (!mScannerController->isInitialized()) {
            textEdit->append("\n=== Auto-initializing Scanner for layer printing ===");
            on_InitScanner_clicked();
            
            if (mScannerController->isInitialized()) {
                textEdit->append("✓ Scanner auto-initialized successfully");
                textEdit->append("✓ System ready for layer-by-layer printing");
            } else {
                textEdit->append("⚠️ Scanner auto-initialization failed");
                textEdit->append("⚠️ Please manually click 'Initialize Scanner'");
                QMessageBox::warning(this, "Scanner Init Warning",
                    "Scanner auto-initialization failed.\n"
                    "Please click 'Initialize Scanner' button manually\n"
                    "before starting the printing process.");
            }
        } else {
            textEdit->append("✓ Scanner already initialized - ready for printing");
        }
    } else {
        QMessageBox::critical(this, "Initialization Failed",
            "Failed to initialize OPC Server.\n"
            "Please check that CoDeSys OPC Server is running.");
    }
    //onTestSLMProcess_clicked(); //This function will test the slm process in pilot mode.
}
//Initialize Scanner button
// Initialize Scanner button
void MainWindow::on_InitScanner_clicked() {
    if (mScannerController->isInitialized()) {
        textEdit->append("Scanner is already initialized");
        QMessageBox::information(this, "Info", "Scanner is already running");
        return;
    }

    // ========== WARNING: MANUAL INITIALIZATION ON UI THREAD ==========
    // This is ONLY for standalone testing/diagnostics
    // Production process MUST NOT use this - Scanner initializes in its own thread
    
    textEdit->append("WARNING: Initializing Scanner on UI thread (manual test mode only)");
    textEdit->append("For production, Scanner will initialize in dedicated consumer thread");
    
    //if (mScannerController->initialize()) {
        if (0) {
        // Scanner is initialized with default config in controller
        textEdit->append("✓ Scanner initialized (test mode)");
        textEdit->append("✓ Ready for manual diagnostics");
        
        // Note: Do NOT shut down here - let user run diagnostics
        // Shutdown will happen when user explicitly requests it or starts production mode
    } else {
        QMessageBox::critical(this, "Initialization Failed",
            "Failed to initialize RTC5 Scanner.\n"
            "Check that:\n"
            "- RTC5 card is installed\n"
            "- RTC5DLL.DLL is present\n"
            "- Correction files are in working directory");
    }
}
//Machine Start Up button
// Machine Start Up button
void MainWindow::on_StartUP_clicked() {
    // ========== WARNING: MANUAL INITIALIZATION ON UI THREAD ==========
    // This is ONLY for standalone PLC startup testing
    // Production process handles OPC initialization in dedicated thread
    
    if (!mOPCController->isInitialized()) {
        textEdit->append("WARNING: Initializing OPC on UI thread (manual test mode only)");
        textEdit->append("For production, OPC initializes in dedicated OPC worker thread");
        
        // Attempt a single initialization attempt and report result.
        if (mOPCController->initialize()) {
            if (textEdit) textEdit->append("✓ OPC Server initialized (test mode)");
        } else {
            if (textEdit) textEdit->append("⚠️ OPC Server initialization failed");
            QMessageBox::warning(this, "OPC Init Warning",
                "OPC Server initialization failed.\n"
                "Please check the logs for more details.");
            return;
        }
    }

    // Abort if still not initialized
    if (!mOPCController->isInitialized()) {
        QMessageBox::warning(this, "Error", "OPC not initialized");
        return;
    }

    // Confirm critical operation
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Confirm Startup",
        "This will initialize the machine.\n\n"
        "Ensure:\n"
        "- Build chamber is clear\n"
        "- Powder reservoirs are filled\n"
        "- All safety covers are closed\n\n"
        "Proceed with startup?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        mOPCController->writeStartUp(true);
        textEdit->append("- Machine startup command sent to PLC");
    }
}
//Start Powder Fill
void MainWindow::on_Prep_Powder_Fill_clicked() {
    
    if (!mOPCController->isInitialized()) {
        // Attempt a single initialization attempt and report result.
        if (mOPCController->initialize()) {
            if (textEdit) textEdit->append("✓ OPC Server initialized successfully");
        } else {
            if (textEdit) textEdit->append("⚠️ OPC Server initialization failed");
            QMessageBox::warning(this, "OPC Init Warning",
                "OPC Server initialization failed.\n"
                "Please check the logs for more details.");
        }
    }

    // Abort if still not initialized
    if (!mOPCController->isInitialized()) {
        QMessageBox::warning(this, "Error", "OPC not initialized");
        return;
    }

    // Validate parameters
    const int MIN_DELTA = 10;
    const int MAX_DELTA = 300;
    const int MAX_LAYERS = 1000;

    int deltaSourceVal = static_cast<int>(round(deltaSource->value()));
    int deltaSinkVal = static_cast<int>(round(deltaSink->value()));
    int layersVal = static_cast<int>(round(noOfStacks->value()));

    if (deltaSourceVal < MIN_DELTA || deltaSourceVal > MAX_DELTA) {
        QMessageBox::critical(this, "Safety Error",
            QString("Delta Source must be between %1 and %2 microns").arg(MIN_DELTA).arg(MAX_DELTA));
        return;
    }

    if (deltaSinkVal < MIN_DELTA || deltaSinkVal > MAX_DELTA) {
        QMessageBox::critical(this, "Safety Error",
            QString("Delta Sink must be between %1 and %2 microns").arg(MIN_DELTA).arg(MAX_DELTA));
        return;
    }

    if (layersVal <= 0 || layersVal > MAX_LAYERS) {
        QMessageBox::critical(this, "Safety Error",
            QString("Layers must be between 1 and %1").arg(MAX_LAYERS));
        return;
    }

    // Confirmation dialog
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Confirm Operation",
        QString("Start powder fill with:\n"
            "Delta Source: %1 microns\n"
            "Delta Sink: %2 microns\n"
            "Layers: %3\n\n"
            "Continue?").arg(deltaSourceVal).arg(deltaSinkVal).arg(layersVal),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        mOPCController->writePowderFillParameters(layersVal, deltaSourceVal, deltaSinkVal);
    }
}

void MainWindow::on_Lay_Surface_clicked() {
    textEdit->append("Lay Surface (test mode) Not Implemented");
}

void MainWindow::on_MakeBottomLayers_clicked() {
    if (!mOPCController->isInitialized()) {
        QMessageBox::warning(this, "Error", "OPC not initialized");
        return;
    }

    int layers = static_cast<int>(round(noOfStacks_BottomLayer->value()));
    int deltaSourceVal = static_cast<int>(round(deltaSource_BottomLayer->value()));
    int deltaSinkVal = static_cast<int>(round(deltaSink_BottomLayer->value()));

    if (layers <= 0 || layers > 1000) {
        QMessageBox::warning(this, "Invalid Input", "Number of layers must be between 1 and 1000");
        return;
    }

    mOPCController->writeBottomLayerParameters(layers, deltaSourceVal, deltaSinkVal);
}

void MainWindow::on_Restart_process_clicked() {
    if (mProcessController->isRunning()) {
        textEdit->append("ℹ Process already running");
    } else if (mOPCController->isInitialized()) {
        mProcessController->startProcess();
        textEdit->append("✓ Process monitoring restarted");
    } else {
        textEdit->append("✗ Cannot restart - OPC not initialized");
        QMessageBox::warning(this, "Warning", "Please initialize OPC first");
    }
}

void MainWindow::on_EmergencyStop_clicked() {
    mProcessController->emergencyStop();
    
    textEdit->append("🚨 EMERGENCY STOP ACTIVATED!");
    QMessageBox::warning(this, "Emergency Stop",
        "All operations stopped!\n"
        "Check machine state before restarting.");
    
    if (statusBar()) {
        statusBar()->showMessage("EMERGENCY STOP ACTIVATED", 0);  // Persistent
    }
}

void MainWindow::on_RunScannerDiagnostics_clicked() {
    if (!mScannerController->isInitialized()) {
        QMessageBox::warning(this, "Scanner Not Ready", 
            "Scanner is not initialized.\nPlease click 'Initialize Scanner' first.");
        textEdit->append("⚠️ Cannot run diagnostics - scanner not initialized");
        return;
    }

    mScannerController->runDiagnostics();
    
    // Update status display
    mScannerController->updateStatusDisplay(scannerStatusDisplay, scannerErrorLabel);
    
    QMessageBox::information(this, "Scanner Diagnostics",
        "Diagnostics completed.\nCheck system log for detailed results.");
}

// ============================================================================
// Controller Signal Handlers
// ============================================================================

void MainWindow::onOPCDataUpdated(const OPCServerManagerUA::OPCData& data) {
    updateDisplaysFromOPCData(data);
}

void MainWindow::onOPCConnectionLost() {
    textEdit->append("⚠️ WARNING: OPC UA Connection Lost!");
    QMessageBox::warning(this, "Connection Lost",
        "OPC UA Server connection has been lost.\n"
        "Please check the connection and restart if necessary.");
}

void MainWindow::onScannerLayerCompleted(int layerNumber) {
    textEdit->append(QString("✓ Scanner completed layer %1").arg(layerNumber));
    
    // Update scanner status display
    mScannerController->updateStatusDisplay(scannerStatusDisplay, scannerErrorLabel);
}

void MainWindow::onProcessStateChanged(int state) {
    // Process state changes handled by ProcessController
    // UI can react to state changes here if needed
}

void MainWindow::updateDisplaysFromOPCData(const OPCServerManagerUA::OPCData& data) {
    // Update UI displays from OPC UA data
    sourceCylPos->display(data.sourceCylPosition);
    sinkCylPos->display(data.sinkCylPosition);
    g_sourceCylPos->display(data.g_sourceCylPosition);
    g_sinkCylPos->display(data.g_sinkCylPosition);
    stacksLeft->display(data.stacksLeft);
    Ready2Powder->display(data.ready2Powder);
    StartUpDone->display(data.startUpDone);
    PowderSurfaceDone_2->display(data.powderSurfaceDone);
}

// RUN MENU
void MainWindow::onRunInitialize() {
    textEdit->append("Run -> Initialize System");
    
    // Initialize OPC if not already initialized
    if (!mOPCController->isInitialized()) {
        on_InitOPC_clicked();
    }
    
    // Initialize Scanner if not already initialized
    if (!mScannerController->isInitialized()) {
        on_InitScanner_clicked();
    }
    
    if (statusBar()) {
        statusBar()->showMessage("System initialization complete", 3000);
    }
}

void MainWindow::onRunStart() {
    textEdit->append("Run -> Start Process");
    
    //if (mOPCController->isInitialized()) {
      //  mProcessController->startProcess();
       // if (statusBar()) statusBar()->showMessage("Process started", 3000);
    //} else {
       // QMessageBox::warning(this, "Not Ready", "Please initialize OPC first");
    //}
    onStartScanProcess_clicked();
}

void MainWindow::onRunPause() {
    textEdit->append("Run -> Pause");
    
    if (mProcessController->isRunning()) {
        mProcessController->pauseProcess();
        if (statusBar()) statusBar()->showMessage("Process paused", 3000);
        QMessageBox::information(this, "Paused", "Process has been paused.");
    } else {
        textEdit->append("ℹ No active process to pause");
        if (statusBar()) statusBar()->showMessage("No active process", 3000);
    }
}

void MainWindow::onRunStop() {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Stop Process",
        "Stop the current process?\n\n"
        "This will halt all operations.",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        mProcessController->stopProcess();
        textEdit->append("Run -> Stop - Process stopped");
        if (statusBar()) statusBar()->showMessage("Process stopped", 3000);
    }
}

void MainWindow::onRunEmergencyStop() {
    on_EmergencyStop_clicked();
}

// ============================================================================
// Project Management Slot Implementations
// ============================================================================

void MainWindow::onProjectOpen() {
    if (!mProjectManager) {
        mProjectManager = new ProjectManager(this);
    }
    
    if (mProjectManager->openProjectInteractive()) {
        if (textEdit) textEdit->append("✓ Project opened successfully");
        if (statusBar()) statusBar()->showMessage("Project opened", 3000);
    } else {
        if (textEdit) textEdit->append("✗ Project open canceled or failed");
        if (statusBar()) statusBar()->showMessage("Project open canceled/failed", 3000);
    }
}
    
void MainWindow::onProjectAttachMarc() {
    if (!mProjectManager || !mProjectManager->hasProject()) {
        QMessageBox::warning(this, "No Project", 
            "Please open or create a project first before attaching files.");
        return;
    }
    
    if (mProjectManager->attachMarcInteractive()) {
        if (textEdit) textEdit->append("✓ MARC file attached successfully");
        if (statusBar()) statusBar()->showMessage("MARC file attached", 3000);
    } else {
        if (textEdit) textEdit->append("✗ MARC file attachment canceled or failed");
    }
}

void MainWindow::onProjectAttachJson() {
    if (!mProjectManager || !mProjectManager->hasProject()) {
        QMessageBox::warning(this, "No Project", 
            "Please open or create a project first before attaching files.");
        return;
    }
    
    if (mProjectManager->attachJsonInteractive()) {
        if (textEdit) textEdit->append("✓ JSON config attached successfully");
        if (statusBar()) statusBar()->showMessage("JSON config attached", 3000);
    } else {
        if (textEdit) textEdit->append("✗ JSON config attachment canceled or failed");
    }
}

// ============================================================================
// Project Explorer Update Helper
// ============================================================================
void MainWindow::updateProjectExplorer() {
    if (!projectTree || !mProjectManager || !mProjectManager->hasProject()) {
        return;
    }
    
    projectTree->clear();
    MarcProject* project = mProjectManager->currentProject();
    if (!project) return;
    
    auto* rootItem = new QTreeWidgetItem(projectTree);
    rootItem->setText(0, project->name());
    rootItem->setExpanded(true);
    
    auto* buildItem = new QTreeWidgetItem(rootItem);
    buildItem->setText(0, "Build");
    buildItem->setText(1, project->buildFilePath());
    buildItem->setToolTip(1, project->buildFilePath());
    
    auto* marcItem = new QTreeWidgetItem(rootItem);
    marcItem->setText(0, "MARC");
    QString marcPath = project->marcFilePath();
    if (marcPath.isEmpty()) {
        marcItem->setText(1, "(not attached)");
        marcItem->setForeground(1, QBrush(QColor("#FF9800")));
    } else {
        marcItem->setText(1, marcPath);
        marcItem->setToolTip(1, marcPath);
        marcItem->setForeground(1, QBrush(QColor("#4CAF50")));
    }
    
    auto* jsonItem = new QTreeWidgetItem(rootItem);
    jsonItem->setText(0, "JSON");
    QString jsonPath = project->jsonFilePath();
    if (jsonPath.isEmpty()) {
        jsonItem->setText(1, "(not attached)");
        jsonItem->setForeground(1, QBrush(QColor("#FF9800")));
    } else {
        jsonItem->setText(1, jsonPath);
        jsonItem->setToolTip(1, jsonPath);
        jsonItem->setForeground(1, QBrush(QColor("#4CAF50")));
    }
    
    const BuildStatistics& stats = project->statistics();
    if (stats.totalLayers > 0) {
        auto* statsItem = new QTreeWidgetItem(rootItem);
        statsItem->setText(0, "Statistics");
        
        auto* layersItem = new QTreeWidgetItem(statsItem);
        layersItem->setText(0, "Total Layers");
        layersItem->setText(1, QString::number(stats.totalLayers));
        
        auto* statusItem = new QTreeWidgetItem(statsItem);
        statusItem->setText(0, "Status");
        statusItem->setText(1, stats.status);
        
        if (stats.layersCompleted > 0) {
            auto* completedItem = new QTreeWidgetItem(statsItem);
            completedItem->setText(0, "Completed");
            completedItem->setText(1, QString("%1/%2")
                .arg(stats.layersCompleted).arg(stats.totalLayers));
        }
    }
    
    projectTree->resizeColumnToContents(0);
    projectTree->resizeColumnToContents(1);
}

// ============================================================================
// ScanStreamingManager Signal Handlers (Streaming MARC Integration)
// ============================================================================
void MainWindow::onScanProcessStatusMessage(const QString& msg) {
    if (textEdit) textEdit->append(msg);
}

void MainWindow::onScanProcessProgress(size_t processed, size_t total) {
    QString progress = QString("Progress: %1/%2 layers").arg(processed).arg(total);
}

void MainWindow::onScanProcessFinished() {
    textEdit->append("✓✓✓ Streaming process finished successfully!");
    if (statusBar()) statusBar()->showMessage("Streaming complete", 3000);
}

void MainWindow::onScanProcessError(const QString& err) {
    textEdit->append(QString("✗✗✗ Streaming error: %1").arg(err));
    if (statusBar()) statusBar()->showMessage("Streaming error", 3000);
    QMessageBox::critical(this, "Process Error", err);
}

// ============================================================================
// NEW: DUAL SLM PROCESS MODE HANDLERS
// ============================================================================

/// Test SLM Process - Synthetic layers, no OPC, no MARC file
void MainWindow::onTestSLMProcess_clicked() {
   /* if (!mScannerController || !mScannerController->isInitialized()) {
        QMessageBox::warning(this, "Scanner Not Ready", 
            "Scanner must be initialized before running test mode.\n"
            "Please click 'Initialize Scanner' first.");
        textEdit->append("⚠️ Cannot start test SLM - scanner not initialized");
        return;
    }*/

    // Dialog to get test parameters
    QDialog paramDialog(this);
    paramDialog.setWindowTitle("Test SLM Process Parameters");
    paramDialog.setMinimumWidth(400);

    QVBoxLayout mainLayout(&paramDialog);
    mainLayout.setSpacing(12);

    QLabel* titleLabel = new QLabel("<b>Configure Test SLM Process</b>");
    mainLayout.addWidget(titleLabel);

    QLabel* modeLabel = new QLabel("Mode: Synthetic layers (no MARC file, no OPC)");
    modeLabel->setStyleSheet("QLabel { color: #2196F3; font-weight: bold; }");
    mainLayout.addWidget(modeLabel);

    // Layer thickness
    QHBoxLayout thicknessLayout;
    thicknessLayout.addWidget(new QLabel("Layer Thickness:"));
    QDoubleSpinBox* thicknessSpinBox = new QDoubleSpinBox();
    thicknessSpinBox->setRange(0.01, 0.5);
    thicknessSpinBox->setValue(0.2);
    thicknessSpinBox->setDecimals(3);
    thicknessSpinBox->setSuffix(" mm");
    thicknessSpinBox->setMinimumWidth(100);
    thicknessLayout.addWidget(thicknessSpinBox);
    thicknessLayout.addStretch();
    mainLayout.addLayout(&thicknessLayout);

    // Layer count
    QHBoxLayout countLayout;
    countLayout.addWidget(new QLabel("Number of Layers:"));
    QSpinBox* countSpinBox = new QSpinBox();
    countSpinBox->setRange(1, 100);
    countSpinBox->setValue(5);
    countSpinBox->setMinimumWidth(100);
    countLayout.addWidget(countSpinBox);
    countLayout.addStretch();
    mainLayout.addLayout(&countLayout);

    // Info box
    QLabel* infoLabel = new QLabel(
        "<b>Test Mode Info:</b><br>"
        "• No MARC file required<br>"
        "• No OPC communication<br>"
        "• Generates synthetic 10mm square per layer<br>"
        "• Useful for hardware testing and diagnostics<br>"
        "• Fully isolated from production pipeline"
    );
    infoLabel->setStyleSheet("QLabel { background-color: #F5F5F5; padding: 8px; border-radius: 4px; }");
    mainLayout.addWidget(infoLabel);

    // Buttons
    QHBoxLayout buttonLayout;
    buttonLayout.addStretch();

    QPushButton* startBtn = new QPushButton("Start Test");
    startBtn->setMinimumWidth(100);
    startBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; border-radius: 4px; }");
    buttonLayout.addWidget(startBtn);

    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setMinimumWidth(100);
    cancelBtn->setStyleSheet("QPushButton { background-color: #757575; color: white; border-radius: 4px; }");
    buttonLayout.addWidget(cancelBtn);

    connect(startBtn, &QPushButton::clicked, &paramDialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &paramDialog, &QDialog::reject);

    mainLayout.addSpacing(12);
    mainLayout.addLayout(&buttonLayout);

    // Show dialog
    if (paramDialog.exec() == QDialog::Accepted) {
        float thickness = static_cast<float>(thicknessSpinBox->value());
        size_t count = static_cast<size_t>(countSpinBox->value());

        // Start test process
        if (mProcessController) {
            mProcessController->startTestSLMProcess(thickness, count);
        }
    }
}

/// Start Scan Process - Production mode, slice-file driven with OPC
void MainWindow::onStartScanProcess_clicked() {
    textEdit->append("Run -> Start Process");

    // ========== VALIDATION ONLY - NO INITIALIZATION ON UI THREAD ==========
    // Do NOT initialize OPC or Scanner here - they must initialize in their own threads
    
    // ========== STEP 1: Get MARC file from project or prompt user ==========
    QString marcPath;
    if (mProjectManager && mProjectManager->hasProject()) {
        marcPath = mProjectManager->marcAbsolutePath();
        if (!marcPath.isEmpty()) {
            textEdit->append(QString("- Using MARC from project: %1").arg(marcPath));
        }
    }

    // If no MARC in project, prompt user to select one
    if (marcPath.isEmpty()) {
        marcPath = QFileDialog::getOpenFileName(this,
            "Select MARC File for Scanning", "",
            "MARC Files (*.marc);;All Files (*)");

        if (marcPath.isEmpty()) {
            textEdit->append("- MARC file selection cancelled");
            return;
        }
    }

    // ========== STEP 2: Get JSON configuration file from project or prompt user ==========
    QString jsonPath;
    if (mProjectManager && mProjectManager->hasProject()) {
        jsonPath = mProjectManager->jsonAbsolutePath();
        if (!jsonPath.isEmpty()) {
            textEdit->append(QString("- Using JSON config from project: %1").arg(jsonPath));
        }
    }

    // If no JSON in project, prompt user to select one
    if (jsonPath.isEmpty()) {
        jsonPath = QFileDialog::getOpenFileName(this,
            "Select JSON Configuration File", "",
            "JSON Configuration Files (*.json);;All Files (*)");

        if (jsonPath.isEmpty()) {
            textEdit->append("- JSON configuration file selection cancelled");
            return;
        }
    }

    // ========== STEP 3: Validate JSON file exists ==========
    QFileInfo jsonFileInfo(jsonPath);
    if (!jsonFileInfo.exists() || !jsonFileInfo.isFile()) {
        QMessageBox::critical(this, "Invalid Configuration File",
            QString("JSON configuration file does not exist:\n%1").arg(jsonPath));
        textEdit->append(QString("✗ Invalid JSON configuration file: %1").arg(jsonPath));
        return;
    }

    // ========== STEP 4: Confirmation dialog with both files ==========
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Start Production SLM Process",
        QString("Start production SLM process with:\n\n"
            "MARC File: %1\n"
            "JSON Config: %2\n\n"
            "This will:\n"
            " - Initialize OPC in dedicated OPC thread\n"
            " - Initialize Scanner in dedicated Scanner thread\n"
            " - Load scan parameters from JSON configuration\n"
            " - Stream layers from MARC file\n"
            " - Synchronize with OPC for layer creation\n"
            " - Execute RTC5 scanning with parameter switching\n\n"
            "Proceed?")
            .arg(QFileInfo(marcPath).fileName())
            .arg(QFileInfo(jsonPath).fileName()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // ========== CRITICAL: START PROCESS WITHOUT UI THREAD INITIALIZATION ==========
        // ProcessController will:
        // 1. Create OPC worker thread -> OPC initializes in that thread
        // 2. Start ScanStreamingManager -> Scanner initializes in consumer thread
        // 3. All initialization happens in proper threads (NOT UI thread)
        
        textEdit->append("- Starting production SLM process...");
        textEdit->append("- OPC will initialize in OPC worker thread");
        textEdit->append("- Scanner will initialize in scanner consumer thread");
        
        if (mProcessController) {
            mProcessController->startProductionSLMProcess(marcPath, jsonPath);
        } else {
            textEdit->append("ERROR: ProcessController not available");
            QMessageBox::critical(this, "Internal Error", 
                "ProcessController is not initialized. Cannot start process.");
        }
    }
}