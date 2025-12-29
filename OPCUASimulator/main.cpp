// OPC UA Simulator Server using open62541
// C++17, no Qt dependencies
// Endpoint: opc.tcp://localhost:4840
// Namespace URI: urn:codesys:dlms:simulation (index defaults to 2)

#include "opcua_sim_server.h"
#include <iostream>
#include <memory>
#include <thread>
#include <string>
#include <atomic>
#include <csignal>
#include <condition_variable>
#include <mutex>

// Global flag for process lifetime control (independent of stdin)
std::atomic<bool> g_should_exit = false;
std::mutex g_signal_mutex;
std::condition_variable g_signal_cv;

// Signal handler for SIGINT (Ctrl+C) and SIGTERM
void signal_handler(int signum) {
    std::cout << "\n[SIGNAL] Received signal " << signum << ", initiating graceful shutdown..." << std::endl;
    g_should_exit = true;
    g_signal_cv.notify_all();
}

// Optional: Monitor stdin in a non-blocking way (separate thread)
void stdin_monitor_thread(OpcUaSimServer* server) {
    std::cout << "[STDIN] Input monitor thread started" << std::endl;
    std::string input;
    while (!g_should_exit) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input)) {
            // stdin failed (closed, EOF, or not available)
            // This is NOT fatal - server continues running
            std::cerr << "[STDIN] Input stream unavailable (expected in background mode)" << std::endl;
            // Sleep to avoid busy-waiting if stdin is broken
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if (input == "q" || input == "Q") {
            std::cout << "[STDIN] User requested shutdown via 'q'" << std::endl;
            g_should_exit = true;
            g_signal_cv.notify_all();
            break;
        } else if (!input.empty()) {
            std::cout << "[CMD] Unknown command. Available: 'q' (quit), 'Ctrl+C' (interrupt)" << std::endl;
        }
    }
    std::cout << "[STDIN] Input monitor thread exiting" << std::endl;
}

int main(int argc, char** argv) {
    try {
        // Register signal handlers
        std::signal(SIGINT, signal_handler);
#ifdef _WIN32
        std::signal(SIGBREAK, signal_handler);
#endif
        std::signal(SIGTERM, signal_handler);

        std::cout << "====== OPC UA Simulator Server ======" << std::endl;
        std::cout << "Endpoint: opc.tcp://localhost:4840" << std::endl;
        std::cout << "Namespace: urn:codesys:dlms:simulation (default index=2)" << std::endl;
        std::cout << "Standard: C++17 | Stack: open62541" << std::endl;
        std::cout << "=====================================" << std::endl;
        std::cout << std::endl;

        // Server and thread lifecycle management
        auto server = std::make_unique<OpcUaSimServer>();
        std::unique_ptr<std::thread> server_thread;
        std::unique_ptr<std::thread> stdin_thread;

        // Configure and start server
        std::cout << "[MAIN] Configuring server..." << std::endl;
        server->configure("opc.tcp://localhost:4840", "urn:codesys:dlms:simulation", 2);
        
        std::cout << "[MAIN] Starting server..." << std::endl;
        if (!server->start()) {
            std::cerr << "[FATAL] Failed to start OPC UA simulator" << std::endl;
            return 1;
        }

        std::cout << "[MAIN] Server started successfully and listening" << std::endl;
        std::cout << "[MAIN] Waiting for client connections..." << std::endl;
        std::cout << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  Press 'q' + Enter to quit gracefully" << std::endl;
        std::cout << "  Press Ctrl+C to interrupt" << std::endl;
        std::cout << std::endl;

        // Launch OPC UA server thread
        server_thread = std::make_unique<std::thread>([&server]() {
            std::cout << "[THREAD] OPC UA server thread started" << std::endl;
            server->run();
            std::cout << "[THREAD] OPC UA server thread exiting" << std::endl;
        });

        // Launch optional stdin monitor thread (non-fatal if unavailable)
        stdin_thread = std::make_unique<std::thread>([&server]() {
            stdin_monitor_thread(server.get());
        });
        stdin_thread->detach();

        // Main thread: Block until signal/shutdown requested
        {
            std::unique_lock<std::mutex> lock(g_signal_mutex);
            while (!g_should_exit) {
                g_signal_cv.wait_for(lock, std::chrono::seconds(5), [](){ return g_should_exit.load(); });
            }
        }

        std::cout << std::endl << "[MAIN] Shutdown initiated, stopping server..." << std::endl;
        server->stop();

        // Join server thread with explicit wait
        if (server_thread && server_thread->joinable()) {
            std::cout << "[MAIN] Waiting for OPC UA server thread to finish..." << std::endl;
            server_thread->join();
            std::cout << "[MAIN] OPC UA server thread joined successfully" << std::endl;
        }

        std::cout << "[MAIN] Server stopped gracefully" << std::endl;
        std::cout << "[EXIT] Process exiting with code 0" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] " << e.what() << std::endl;
        std::cerr << "[EXIT] Process exiting with code 2 (exception)" << std::endl;
        return 2;
    }
}
