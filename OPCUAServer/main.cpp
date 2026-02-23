/**
 * @file main.cpp
 * @brief Entry point for the SLM OPC UA Production Server
 * 
 * @details This is the main executable for the OPC UA server that controls
 * a Selective Laser Melting (SLM) machine. It provides a robust, production-
 * grade interface between the control software and PLCs/hardware.
 * 
 * @par Usage:
 * @code
 * OPCUAServer [options]
 * 
 * Options:
 *   --help, -h          Show this help message
 *   --endpoint URL      OPC UA endpoint (default: opc.tcp://0.0.0.0:4840)
 *   --namespace URI     Namespace URI (default: urn:CODESYS:MaTe_DLMS)
 *   --simulate          Enable PLC simulation mode (default)
 *   --production        Disable PLC simulation (real hardware)
 *   --no-failsafe       Disable fail-safe controller (testing only)
 *   --verbose           Enable verbose logging
 * @endcode
 * 
 * @par Environment Variables:
 * - OPC_UA_ENDPOINT: Override default endpoint URL
 * - OPC_UA_NAMESPACE_URI: Override namespace URI
 * - OPC_UA_SIMULATE: Set to "0" for production mode
 * 
 * @par Signal Handling:
 * - SIGINT (Ctrl+C): Clean shutdown
 * - SIGTERM: Clean shutdown
 * 
 * @par Exit Codes:
 * - 0: Clean shutdown
 * - 1: Configuration error
 * - 2: Startup failure
 * - 3: Runtime error
 * 
 * @author Senior Embedded Systems Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#include "slm_opcua_server.h"

#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>

// ============================================================================
// Global State for Signal Handling
// ============================================================================

namespace {
    std::atomic<bool> g_shutdownRequested{false};
    slm_opcua::SlmOpcUaServer* g_serverPtr = nullptr;
}

// ============================================================================
// Signal Handlers
// ============================================================================

/**
 * @brief Handle termination signals for clean shutdown
 * 
 * @param signal Signal number (SIGINT or SIGTERM)
 * 
 * @details This handler ensures the OPC UA server performs a clean shutdown
 * when the process receives a termination signal. It:
 * 1. Sets the global shutdown flag
 * 2. Requests the server to stop
 * 3. Allows the main loop to complete gracefully
 */
void signalHandler(int signal)
{
    std::cout << "\n[SIGNAL] Received signal " << signal << std::endl;
    
    g_shutdownRequested.store(true);
    
    if (g_serverPtr) {
        g_serverPtr->stop();
    }
}

/**
 * @brief Install signal handlers for clean shutdown
 */
void installSignalHandlers()
{
#ifdef _WIN32
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#else
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
}

// ============================================================================
// Command Line Parsing
// ============================================================================

/**
 * @brief Print usage information
 */
void printUsage(const char* programName)
{
    std::cout << "SLM OPC UA Production Server\n"
              << "============================\n\n"
              << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  --help, -h          Show this help message\n"
              << "  --endpoint URL      OPC UA endpoint (default: opc.tcp://0.0.0.0:4840)\n"
              << "  --namespace URI     Namespace URI (default: urn:CODESYS:MaTe_DLMS)\n"
              << "  --simulate          Enable PLC simulation mode (default)\n"
              << "  --production        Disable PLC simulation (real hardware)\n"
              << "  --no-failsafe       Disable fail-safe controller (testing only)\n"
              << "  --verbose           Enable verbose logging\n"
              << "\n"
              << "Environment Variables:\n"
              << "  OPC_UA_ENDPOINT         Override default endpoint URL\n"
              << "  OPC_UA_NAMESPACE_URI    Override namespace URI\n"
              << "  OPC_UA_SIMULATE         Set to \"0\" for production mode\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << " --simulate\n"
              << "  " << programName << " --production --endpoint opc.tcp://192.168.1.10:4840\n"
              << std::endl;
}

/**
 * @brief Parse command line arguments
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @param config Output configuration
 * @return true if parsing successful, false to exit
 */
bool parseCommandLine(int argc, char* argv[], slm_opcua::ServerConfig& config)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--endpoint" && i + 1 < argc) {
            config.endpointUrl = argv[++i];
        }
        else if (arg == "--namespace" && i + 1 < argc) {
            config.namespaceUri = argv[++i];
        }
        else if (arg == "--simulate") {
            config.simulatePlc = true;
        }
        else if (arg == "--production") {
            config.simulatePlc = false;
        }
        else if (arg == "--no-failsafe") {
            config.enableFailSafe = false;
            std::cout << "[WARNING] Fail-safe controller DISABLED - testing mode only!\n";
        }
        else if (arg == "--verbose") {
            // Verbose mode - could be used for additional logging
        }
        else {
            std::cerr << "[ERROR] Unknown option: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Apply environment variable overrides
 */
void applyEnvironmentOverrides(slm_opcua::ServerConfig& config)
{
    // Endpoint URL
    const char* endpoint = std::getenv("OPC_UA_ENDPOINT");
    if (endpoint && *endpoint) {
        config.endpointUrl = endpoint;
        std::cout << "[CONFIG] Endpoint from environment: " << endpoint << std::endl;
    }
    
    // Namespace URI
    const char* nsUri = std::getenv("OPC_UA_NAMESPACE_URI");
    if (nsUri && *nsUri) {
        config.namespaceUri = nsUri;
        std::cout << "[CONFIG] Namespace from environment: " << nsUri << std::endl;
    }
    
    // Simulation mode
    const char* simulate = std::getenv("OPC_UA_SIMULATE");
    if (simulate) {
        config.simulatePlc = (std::string(simulate) != "0");
        std::cout << "[CONFIG] Simulate from environment: " 
                  << (config.simulatePlc ? "YES" : "NO") << std::endl;
    }
}

// ============================================================================
// Startup Banner
// ============================================================================

void printBanner()
{
    std::cout << R"(
?????????????????????????????????????????????????????????????????????????
?                                                                       ?
?   ???????????     ????   ????     ??????? ???????  ???????           ?
?   ???????????     ????? ?????    ?????????????????????????           ?
?   ???????????     ???????????    ???   ??????????????                ?
?   ???????????     ???????????    ???   ?????????? ???                ?
?   ??????????????????? ??? ???    ????????????     ????????           ?
?   ???????????????????     ???     ??????? ???      ???????           ?
?                                                                       ?
?   SLM OPC UA Production Server                                        ?
?   Industrial Control System for Selective Laser Melting               ?
?                                                                       ?
?   Version: 1.0.0                                                      ?
?   Library: open62541                                                  ?
?                                                                       ?
?????????????????????????????????????????????????????????????????????????
)" << std::endl;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[])
{
    printBanner();
    
    // Create default configuration for PRODUCTION MODE (Real Hardware)
    slm_opcua::ServerConfig config;
    config.endpointUrl = "opc.tcp://localhost:4840";    // Expose OPC UA on localhost
    config.namespaceUri = "urn:CODESYS:CECC-D";         // Actual CoDeSys namespace
    config.namespaceIndex = 2;                           // Will be auto-assigned by server
    config.pollingIntervalMs = 10;
    config.enableFailSafe = true;
    config.watchdogTimeoutMs = 5000;
    config.simulatePlc = false;  // DEFAULT TO PRODUCTION MODE - Connect to real PLC
    config.simLayerPrepTimeMs = 100;
    
    // Parse command line
    if (!parseCommandLine(argc, argv, config)) {
        return 1;  // Exit requested or error
    }
    
    // Apply environment overrides (higher priority than command line)
    applyEnvironmentOverrides(config);
    
    // Print configuration summary
    std::cout << "[CONFIG] ============================================\n";
    std::cout << "[CONFIG] Server Configuration:\n";
    std::cout << "[CONFIG]   Endpoint:      " << config.endpointUrl << "\n";
    std::cout << "[CONFIG]   Namespace:     " << config.namespaceUri << "\n";
    std::cout << "[CONFIG]   NS Index:      " << config.namespaceIndex << " (will be auto-assigned)\n";
    std::cout << "[CONFIG]   Polling:       " << config.pollingIntervalMs << " ms\n";
    std::cout << "[CONFIG]   Fail-Safe:     " << (config.enableFailSafe ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "[CONFIG]   Mode:          " << (config.simulatePlc ? "SIMULATION" : "PRODUCTION") << "\n";
    std::cout << "[CONFIG] ============================================\n";
    
    // Production mode info
    if (!config.simulatePlc) {
        std::cout << "\n";
        std::cout << "[INFO] ============================================\n";
        std::cout << "[INFO] PRODUCTION MODE - CONNECTING TO REAL HARDWARE\n";
        std::cout << "[INFO] ============================================\n";
        std::cout << "[INFO] Server will attempt to connect to:\n";
        std::cout << "[INFO]   - PLC IP: 192.168.1.10 (or localhost if on same machine)\n";
        std::cout << "[INFO]   - Protocol: Modbus TCP\n";
        std::cout << "[INFO]   - Port: 502\n";
        std::cout << "[INFO] \n";
        std::cout << "[INFO] If PLC is not accessible, server will run in DEGRADED MODE\n";
        std::cout << "[INFO] (OPC UA server will still be accessible for testing)\n";
        std::cout << "[INFO] ============================================\n";
        std::cout << "\n";
    } else {
        std::cout << "\n";
        std::cout << "[INFO] ============================================\n";
        std::cout << "[INFO] SIMULATION MODE - NO HARDWARE CONNECTION\n";
        std::cout << "[INFO] ============================================\n";
        std::cout << "[INFO] Server is running in SIMULATION mode.\n";
        std::cout << "[INFO] PLC behavior will be emulated internally.\n";
        std::cout << "[INFO] To connect to real hardware, restart with:\n";
        std::cout << "[INFO]   OPCUAServer.exe --production\n";
        std::cout << "[INFO] ============================================\n";
        std::cout << "\n";
    }
    
    // Install signal handlers for clean shutdown
    installSignalHandlers();
    
    // Create server
    slm_opcua::SlmOpcUaServer server(config);
    g_serverPtr = &server;
    
    // Start server
    std::cout << "[MAIN] Starting OPC UA server...\n";
    std::cout << "[MAIN] OPC UA will expose variables at ns=2 (auto-assigned)\n";
    std::cout << "[MAIN] Node ID format: ns=2;s=|var|CECC-D.Application...\n";
    
    if (!server.start()) {
        std::cerr << "[ERROR] Failed to start OPC UA server!\n";
        return 2;
    }
    
    std::cout << "[MAIN] Server started successfully.\n";
    std::cout << "[MAIN] Listening on: " << config.endpointUrl << "\n";
    
    if (!config.simulatePlc) {
        std::cout << "[MAIN] Production mode: Attempting PLC connection...\n";
        std::cout << "[MAIN] Check logs above for PLC connection status.\n";
    }
    
    std::cout << "[MAIN] Press Ctrl+C to shutdown.\n\n";
    
    // Run main server loop (blocks until stop() called)
    try {
        server.run();
    }
    catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] Runtime error: " << e.what() << "\n";
        
        // Attempt emergency stop
        if (config.enableFailSafe) {
            server.failSafe().triggerEmergencyStop(
                std::string("Exception: ") + e.what());
        }
        
        return 3;
    }
    
    // Clean shutdown
    std::cout << "\n[MAIN] Server shutdown complete.\n";
    std::cout << "[MAIN] Goodbye!\n";
    
    g_serverPtr = nullptr;
    return 0;
}
