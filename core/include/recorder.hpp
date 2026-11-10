#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <ixwebsocket/IXWebSocket.h>

namespace polymarket {

struct RecorderConfig {
    std::string ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
    std::vector<std::string> token_ids;
    std::string output_path = "data/polymarket_recording.jsonl";
    int ping_interval_secs = 20;
    int min_reconnect_ms = 1000;
    int max_reconnect_ms = 10000;
};

class RawRecorder {
public:
    explicit RawRecorder(RecorderConfig config);
    ~RawRecorder();

    // Start recording connection
    void start();

    // Stop recording and flush log file
    void stop();

    // Status queries
    bool is_connected() const { return connected_.load(); }
    uint64_t message_count() const { return message_count_.load(); }
    uint64_t reconnect_count() const { return reconnect_count_.load(); }

    const RecorderConfig& get_config() const { return config_; }

private:
    void on_websocket_event(const ix::WebSocketMessagePtr& msg);
    void write_entry(uint64_t recv_time_ns, const std::string& raw_payload);
    std::string build_subscribe_payload() const;

    RecorderConfig config_;
    ix::WebSocket web_socket_;
    std::ofstream out_file_;
    std::mutex file_mutex_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> message_count_{0};
    std::atomic<uint64_t> reconnect_count_{0};
};

} // namespace polymarket
