#include "orderbook.hpp"
#include "tcp_publisher.hpp"
#include "recorder.hpp"
#include "replayer.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <mutex>
#include <csignal>
#include <atomic>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXNetSystem.h>

using json = nlohmann::json;
using namespace polymarket;

static std::atomic<bool> g_engine_running{true};

void handle_signal(int) {
    std::cout << "\n[LiveEngine] Shutting down...\n";
    g_engine_running.store(false);
}

// 8 liquid outcome tokens across 4 markets (matching docs/feed-notes.md & data/polymarket_recording.jsonl)
const std::vector<std::string> DEFAULT_ASSET_IDS = {
    "54533043819946592547517511176940999955633860128497669742211153063842200957669", // Gavin Newsom Yes
    "87854174148074652060467921081181402357467303721471806610111179101805869578687", // Gavin Newsom No
    "32338220190071351435772801779725302244575775216413325951443816017994629993401", // Xi Jinping Yes
    "25659310674993675562345759665114759892400026242514633218387667107987341231962", // Xi Jinping No
    "91031279171981959197361710127213577102576826515163085396576756946418341946256", // Pete Buttigieg Yes
    "18494065527924073750217237765784380365430319396356251245973222791140100738948", // Pete Buttigieg No
    "107064985435494333113391038470401719113272800530429703182710416066774068907304", // Alexandria Ocasio-Cortez Yes
    "65176072261324737856085688071627118509549293922582857186996392180609764586527"   // Alexandria Ocasio-Cortez No
};

void broadcast_all_snapshots(const std::unordered_map<std::string, OrderBook>& books, TcpPublisher& publisher) {
    for (const auto& kv : books) {
        if (kv.second.has_bids() || kv.second.has_asks()) {
            publisher.broadcast(TcpPublisher::format_book_message(kv.second, "snapshot"));
        }
    }
}

void run_replay_mode(const std::string& filepath, double speed, bool loop, TcpPublisher& publisher) {
    std::cout << "[LiveEngine] Starting Replay Mode: " << filepath << " (speed: " << speed << "x, loop: " << (loop ? "yes" : "no") << ")\n";

    std::unordered_map<std::string, OrderBook> books;
    for (const auto& id : DEFAULT_ASSET_IDS) {
        books.emplace(id, OrderBook(id));
    }

    // Wait briefly in looping interactive mode until Gateway connects so initial line-1 snapshots aren't dropped
    if (loop && publisher.client_count() == 0) {
        std::cout << "[LiveEngine] Waiting for gateway client before streaming initial snapshot...\n";
        while (g_engine_running.load() && publisher.client_count() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // Small grace period to ensure client socket recv loop is ready
        if (g_engine_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    uint64_t total_updates = 0;
    size_t last_client_count = publisher.client_count();
    auto start_wall_time = std::chrono::steady_clock::now();

    do {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "[LiveEngine] Could not open recording file: " << filepath << "\n";
            return;
        }

        std::string line;
        uint64_t prev_recv_ns = 0;

        while (g_engine_running.load() && std::getline(file, line)) {
            if (line.empty()) continue;

            // If a new gateway client connected mid-stream, immediately sync it with full book snapshots
            size_t current_clients = publisher.client_count();
            if (current_clients > last_client_count) {
                broadcast_all_snapshots(books, publisher);
            }
            last_client_count = current_clients;

            try {
                json j = json::parse(line);
                if (!j.contains("raw")) continue;

                uint64_t current_recv_ns = j.value("recv_time_ns", 0ULL);
                if (speed > 0.0 && prev_recv_ns > 0 && current_recv_ns > prev_recv_ns) {
                    int64_t delta_ns = current_recv_ns - prev_recv_ns;
                    delta_ns = std::min<int64_t>(delta_ns, 250000000LL); // 250ms max idle gap
                    int64_t sleep_ns = static_cast<int64_t>(delta_ns / speed);
                    if (sleep_ns > 5000) {
                        std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
                    }
                }
                prev_recv_ns = current_recv_ns;

                auto& raw = j["raw"];

                // Array: initial snapshots
                if (raw.is_array()) {
                    for (auto& item : raw) {
                        if (!item.contains("asset_id")) continue;
                        std::string asset_id = item["asset_id"].get<std::string>();
                        std::string market = item.value("market", "");
                        std::string hash = item.value("hash", "");
                        uint64_t ts = item.contains("timestamp") && item["timestamp"].is_string() ?
                                      std::stoull(item["timestamp"].get<std::string>()) : 0;

                        std::vector<std::pair<int64_t, int64_t>> bids, asks;
                        if (item.contains("bids") && item["bids"].is_array()) {
                            for (auto& b : item["bids"]) {
                                bids.emplace_back(parse_price_str(b["price"].get<std::string>()),
                                                  parse_qty_str(b["size"].get<std::string>()));
                            }
                        }
                        if (item.contains("asks") && item["asks"].is_array()) {
                            for (auto& a : item["asks"]) {
                                asks.emplace_back(parse_price_str(a["price"].get<std::string>()),
                                                  parse_qty_str(a["size"].get<std::string>()));
                            }
                        }

                        if (books.find(asset_id) == books.end()) {
                            books.emplace(asset_id, OrderBook(asset_id, market));
                        }
                        auto& book = books[asset_id];
                        book.apply_snapshot(bids, asks, hash, ts);
                        total_updates++;

                        publisher.broadcast(TcpPublisher::format_book_message(book, "snapshot"));
                    }
                }
                // Object: single event
                else if (raw.is_object()) {
                    std::string event_type = raw.value("event_type", "");

                    if (event_type == "book") {
                        if (!raw.contains("asset_id")) continue;
                        std::string asset_id = raw["asset_id"].get<std::string>();
                        std::string market = raw.value("market", "");
                        std::string hash = raw.value("hash", "");
                        uint64_t ts = raw.contains("timestamp") && raw["timestamp"].is_string() ?
                                      std::stoull(raw["timestamp"].get<std::string>()) : 0;

                        std::vector<std::pair<int64_t, int64_t>> bids, asks;
                        if (raw.contains("bids") && raw["bids"].is_array()) {
                            for (auto& b : raw["bids"]) {
                                bids.emplace_back(parse_price_str(b["price"].get<std::string>()),
                                                  parse_qty_str(b["size"].get<std::string>()));
                            }
                        }
                        if (raw.contains("asks") && raw["asks"].is_array()) {
                            for (auto& a : raw["asks"]) {
                                asks.emplace_back(parse_price_str(a["price"].get<std::string>()),
                                                  parse_qty_str(a["size"].get<std::string>()));
                            }
                        }

                        if (books.find(asset_id) == books.end()) {
                            books.emplace(asset_id, OrderBook(asset_id, market));
                        }
                        auto& book = books[asset_id];
                        book.apply_snapshot(bids, asks, hash, ts);
                        total_updates++;

                        publisher.broadcast(TcpPublisher::format_book_message(book, "snapshot"));
                    } else if (event_type == "price_change") {
                        if (raw.contains("price_changes") && raw["price_changes"].is_array()) {
                            uint64_t ts = raw.contains("timestamp") && raw["timestamp"].is_string() ?
                                          std::stoull(raw["timestamp"].get<std::string>()) : 0;

                            for (auto& change : raw["price_changes"]) {
                                if (!change.contains("asset_id")) continue;
                                std::string asset_id = change["asset_id"].get<std::string>();
                                std::string price_str = change["price"].get<std::string>();
                                std::string size_str = change["size"].get<std::string>();
                                std::string side_str = change["side"].get<std::string>();

                                Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                                int64_t price_ticks = parse_price_str(price_str);
                                int64_t size_units = parse_qty_str(size_str);

                                if (books.find(asset_id) == books.end()) {
                                    books.emplace(asset_id, OrderBook(asset_id));
                                }
                                auto& book = books[asset_id];
                                book.apply_delta(side, price_ticks, size_units, ts);
                                total_updates++;

                                publisher.broadcast(TcpPublisher::format_book_message(book, "book_update"));
                            }
                        }
                    } else if (event_type == "last_trade_price") {
                        // Forward trade execution event
                        json trade_msg;
                        trade_msg["event"] = "trade";
                        trade_msg["asset_id"] = raw.value("asset_id", "");
                        trade_msg["market_id"] = raw.value("market", "");
                        trade_msg["price"] = raw.value("price", "0");
                        trade_msg["size"] = raw.value("size", "0");
                        trade_msg["side"] = raw.value("side", "BUY");
                        trade_msg["timestamp_ms"] = raw.contains("timestamp") && raw["timestamp"].is_string() ?
                                                    std::stoull(raw["timestamp"].get<std::string>()) : 0;
                        publisher.broadcast(trade_msg.dump());
                    }
                }

                // Periodic status heartbeat
                if (total_updates % 250 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    double elapsed = std::chrono::duration<double>(now - start_wall_time).count();
                    json hb;
                    hb["event"] = "heartbeat";
                    hb["total_updates"] = total_updates;
                    hb["clients"] = publisher.client_count();
                    hb["active_books"] = books.size();
                    hb["uptime_seconds"] = elapsed;
                    publisher.broadcast(hb.dump());
                }

            } catch (const std::exception& e) {
                // Ignore corrupt line in log
            }
        }

        std::cout << "[LiveEngine] Completed pass over dataset. Total updates: " << total_updates << "\n";
        if (loop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } while (loop && g_engine_running.load());
}

void run_live_mode(TcpPublisher& publisher) {
    std::cout << "[LiveEngine] Starting Live Polymarket WebSocket Mode...\n";

    std::mutex books_mutex;
    std::unordered_map<std::string, OrderBook> books;
    for (const auto& id : DEFAULT_ASSET_IDS) {
        books.emplace(id, OrderBook(id));
    }

    std::atomic<uint64_t> total_updates{0};

    RecorderConfig config;
    config.ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
    config.token_ids = DEFAULT_ASSET_IDS;
    config.output_path = ""; // Do not record to disk in live engine mode

    RawRecorder recorder(config);
    recorder.set_message_callback([&](const std::string& raw_msg, uint64_t recv_time_ns) {
        (void)recv_time_ns;
        try {
            json raw = json::parse(raw_msg);
            std::lock_guard<std::mutex> lock(books_mutex);

            // 1. Initial Snapshot Array
            if (raw.is_array()) {
                for (auto& item : raw) {
                    if (!item.contains("asset_id")) continue;
                    std::string asset_id = item["asset_id"].get<std::string>();
                    std::string market = item.value("market", "");
                    std::string hash = item.value("hash", "");
                    uint64_t ts = item.contains("timestamp") && item["timestamp"].is_string() ?
                                  std::stoull(item["timestamp"].get<std::string>()) : 0;

                    std::vector<std::pair<int64_t, int64_t>> bids, asks;
                    if (item.contains("bids") && item["bids"].is_array()) {
                        for (auto& b : item["bids"]) {
                            bids.emplace_back(parse_price_str(b["price"].get<std::string>()),
                                              parse_qty_str(b["size"].get<std::string>()));
                        }
                    }
                    if (item.contains("asks") && item["asks"].is_array()) {
                        for (auto& a : item["asks"]) {
                            asks.emplace_back(parse_price_str(a["price"].get<std::string>()),
                                              parse_qty_str(a["size"].get<std::string>()));
                        }
                    }

                    if (books.find(asset_id) == books.end()) {
                        books.emplace(asset_id, OrderBook(asset_id, market));
                    }
                    auto& book = books[asset_id];
                    book.apply_snapshot(bids, asks, hash, ts);
                    total_updates++;

                    publisher.broadcast(TcpPublisher::format_book_message(book, "snapshot"));
                }
            }
            // 2. Event Object
            else if (raw.is_object()) {
                std::string event_type = raw.value("event_type", "");

                if (event_type == "book") {
                    if (!raw.contains("asset_id")) return;
                    std::string asset_id = raw["asset_id"].get<std::string>();
                    std::string market = raw.value("market", "");
                    std::string hash = raw.value("hash", "");
                    uint64_t ts = raw.contains("timestamp") && raw["timestamp"].is_string() ?
                                  std::stoull(raw["timestamp"].get<std::string>()) : 0;

                    std::vector<std::pair<int64_t, int64_t>> bids, asks;
                    if (raw.contains("bids") && raw["bids"].is_array()) {
                        for (auto& b : raw["bids"]) {
                            bids.emplace_back(parse_price_str(b["price"].get<std::string>()),
                                              parse_qty_str(b["size"].get<std::string>()));
                        }
                    }
                    if (raw.contains("asks") && raw["asks"].is_array()) {
                        for (auto& a : raw["asks"]) {
                            asks.emplace_back(parse_price_str(a["price"].get<std::string>()),
                                              parse_qty_str(a["size"].get<std::string>()));
                        }
                    }

                    if (books.find(asset_id) == books.end()) {
                        books.emplace(asset_id, OrderBook(asset_id, market));
                    }
                    auto& book = books[asset_id];
                    book.apply_snapshot(bids, asks, hash, ts);
                    total_updates++;

                    publisher.broadcast(TcpPublisher::format_book_message(book, "snapshot"));
                } else if (event_type == "price_change") {
                    if (raw.contains("price_changes") && raw["price_changes"].is_array()) {
                        uint64_t ts = raw.contains("timestamp") && raw["timestamp"].is_string() ?
                                      std::stoull(raw["timestamp"].get<std::string>()) : 0;

                        for (auto& change : raw["price_changes"]) {
                            if (!change.contains("asset_id")) continue;
                            std::string asset_id = change["asset_id"].get<std::string>();
                            std::string price_str = change["price"].get<std::string>();
                            std::string size_str = change["size"].get<std::string>();
                            std::string side_str = change["side"].get<std::string>();

                            Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                            int64_t price_ticks = parse_price_str(price_str);
                            int64_t size_units = parse_qty_str(size_str);

                            if (books.find(asset_id) == books.end()) {
                                books.emplace(asset_id, OrderBook(asset_id));
                            }
                            auto& book = books[asset_id];
                            book.apply_delta(side, price_ticks, size_units, ts);
                            total_updates++;

                            publisher.broadcast(TcpPublisher::format_book_message(book, "book_update"));
                        }
                    }
                } else if (event_type == "last_trade_price") {
                    json trade_msg;
                    trade_msg["event"] = "trade";
                    trade_msg["asset_id"] = raw.value("asset_id", "");
                    trade_msg["market_id"] = raw.value("market", "");
                    trade_msg["price"] = raw.value("price", "0");
                    trade_msg["size"] = raw.value("size", "0");
                    trade_msg["side"] = raw.value("side", "BUY");
                    trade_msg["timestamp_ms"] = raw.contains("timestamp") && raw["timestamp"].is_string() ?
                                                std::stoull(raw["timestamp"].get<std::string>()) : 0;
                    publisher.broadcast(trade_msg.dump());
                }
            }
        } catch (const std::exception& e) {
            // Ignore parse errors
        }
    });

    recorder.start();
    std::cout << "[LiveEngine] Initiating connection to Polymarket WebSocket feed...\n";

    std::cout << "[LiveEngine] Connected and publishing live market feed to TCP clients.\n";
    auto start_time = std::chrono::steady_clock::now();
    size_t last_client_count = publisher.client_count();
    int tick_counter = 0;

    while (g_engine_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        size_t current_clients = publisher.client_count();
        if (current_clients > last_client_count) {
            std::lock_guard<std::mutex> lock(books_mutex);
            broadcast_all_snapshots(books, publisher);
        }
        last_client_count = current_clients;

        if (++tick_counter % 10 == 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();

            size_t active_books_count = 0;
            {
                std::lock_guard<std::mutex> lock(books_mutex);
                active_books_count = books.size();
            }

            json hb;
            hb["event"] = "heartbeat";
            hb["total_updates"] = total_updates.load();
            hb["clients"] = current_clients;
            hb["active_books"] = active_books_count;
            hb["uptime_seconds"] = elapsed;
            publisher.broadcast(hb.dump());
        }
    }

    recorder.stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    ix::initNetSystem();

    std::string mode = "replay";
    std::string filepath = "data/polymarket_recording.jsonl";
    double speed = 1.0;
    bool loop = true;
    int port = 9001;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--live") {
            mode = "live";
        } else if (arg == "--replay" && i + 1 < argc) {
            mode = "replay";
            filepath = argv[++i];
        } else if (arg == "--speed" && i + 1 < argc) {
            speed = std::stod(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--no-loop") {
            loop = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: live_engine [options]\n"
                      << "  --live               Connect to live Polymarket WebSocket feed\n"
                      << "  --replay <path>      Replay recorded JSONL file (default: data/polymarket_recording.jsonl)\n"
                      << "  --speed <mult>       Replay speed multiplier (default: 1.0, 0 for max)\n"
                      << "  --port <port>        TCP publisher port (default: 9001)\n"
                      << "  --no-loop            Exit after single pass (replay mode only)\n";
            return 0;
        }
    }

    TcpPublisher publisher(port);
    if (!publisher.start()) {
        std::cerr << "[LiveEngine] Failed to bind TCP server on port " << port << "\n";
        ix::uninitNetSystem();
        return 1;
    }

    if (mode == "live") {
        run_live_mode(publisher);
    } else {
        run_replay_mode(filepath, speed, loop, publisher);
    }

    publisher.stop();
    ix::uninitNetSystem();
    std::cout << "[LiveEngine] Engine stopped gracefully.\n";
    return 0;
}
