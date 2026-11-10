#include "recorder.hpp"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <ixwebsocket/IXNetSystem.h>

using namespace polymarket;

static std::atomic<bool> g_shutdown{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[SIGNAL] Received shutdown signal. Exiting cleanly..." << std::endl;
        g_shutdown.store(true);
    }
}

int main(int argc, char* argv[]) {
    ix::initNetSystem();
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    RecorderConfig config;
    // Default tokens from Phase 0 feed-notes.md
    config.token_ids = {
        // Gavin Newsom 2028 (YES / NO)
        "54533043819946592547517511176940999955633860128497669742211153063842200957669",
        "87854174148074652060467921081181402357467303721471806610111179101805869578687",
        // Xi Jinping 2027 (YES / NO)
        "32338220190071351435772801779725302244575775216413325951443816017994629993401",
        "25659310674993675562345759665114759892400026242514633218387667107987341231962",
        // Pete Buttigieg 2028 (YES / NO)
        "91031279171981959197361710127213577102576826515163085396576756946418341946256",
        "18494065527924073750217237765784380365430319396356251245973222791140100738948",
        // Alexandria Ocasio-Cortez 2028 (YES / NO)
        "107064985435494333113391038470401719113272800530429703182710416066774068907304",
        "65176072261324737856085688071627118509549293922582857186996392180609764586527"
    };

    int duration_seconds = 0; // 0 = indefinite until Ctrl+C

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) {
            config.output_path = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            duration_seconds = std::stoi(argv[++i]);
        } else if (arg == "--url" && i + 1 < argc) {
            config.ws_url = argv[++i];
        }
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "Polymarket Raw Feed Recorder" << std::endl;
    std::cout << "WebSocket Endpoint: " << config.ws_url << std::endl;
    std::cout << "Subscribed Tokens: " << config.token_ids.size() << std::endl;
    std::cout << "Destination Log: " << config.output_path << std::endl;
    if (duration_seconds > 0) {
        std::cout << "Planned Duration: " << duration_seconds << " seconds" << std::endl;
    } else {
        std::cout << "Running indefinitely (press Ctrl+C to terminate)" << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    RawRecorder recorder(config);
    recorder.start();

    auto start_time = std::chrono::steady_clock::now();

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (duration_seconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time
            ).count();
            if (elapsed >= duration_seconds) {
                std::cout << "[RECORDER] Requested duration of " << duration_seconds << "s elapsed." << std::endl;
                break;
            }
        }
    }

    recorder.stop();
    ix::uninitNetSystem();
    std::cout << "==========================================================" << std::endl;
    std::cout << "Recording Session Completed" << std::endl;
    std::cout << "Total Captured Messages: " << recorder.message_count() << std::endl;
    std::cout << "Total Reconnects: " << recorder.reconnect_count() << std::endl;
    std::cout << "Log saved to: " << config.output_path << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
