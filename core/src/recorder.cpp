#include "recorder.hpp"
#include <iostream>
#include <filesystem>
#include <sstream>
#include <ixwebsocket/IXNetSystem.h>

namespace polymarket {

RawRecorder::RawRecorder(RecorderConfig config)
    : config_(std::move(config)) {
    // Ensure output directory exists if output_path is provided
    if (!config_.output_path.empty()) {
        std::filesystem::path p(config_.output_path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        out_file_.open(config_.output_path, std::ios::out | std::ios::app);
        if (!out_file_.is_open()) {
            throw std::runtime_error("Failed to open output recording file: " + config_.output_path);
        }
    }

    // Configure IXWebSocket
    web_socket_.setUrl(config_.ws_url);
    web_socket_.setPingInterval(config_.ping_interval_secs);
    web_socket_.enableAutomaticReconnection();
    web_socket_.setMinWaitBetweenReconnectionRetries(config_.min_reconnect_ms);
    web_socket_.setMaxWaitBetweenReconnectionRetries(config_.max_reconnect_ms);

    web_socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        this->on_websocket_event(msg);
    });
}

RawRecorder::~RawRecorder() {
    stop();
}

std::string RawRecorder::build_subscribe_payload() const {
    std::ostringstream ss;
    ss << "{\"assets_ids\":[";
    for (size_t i = 0; i < config_.token_ids.size(); ++i) {
        ss << "\"" << config_.token_ids[i] << "\"";
        if (i + 1 < config_.token_ids.size()) {
            ss << ",";
        }
    }
    ss << "],\"type\":\"market\",\"custom_feature_enabled\":true}";
    return ss.str();
}

void RawRecorder::on_websocket_event(const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
        connected_.store(true);
        reconnect_count_.fetch_add(1);
        std::cout << "[RECORDER] Connected to " << config_.ws_url << std::endl;

        std::string sub = build_subscribe_payload();
        std::cout << "[RECORDER] Sending subscription for " << config_.token_ids.size() << " tokens..." << std::endl;
        web_socket_.send(sub);
    } else if (msg->type == ix::WebSocketMessageType::Close) {
        connected_.store(false);
        std::cout << "[RECORDER] WebSocket closed: code " << msg->closeInfo.code
                  << " (" << msg->closeInfo.reason << ")" << std::endl;
    } else if (msg->type == ix::WebSocketMessageType::Error) {
        connected_.store(false);
        std::cerr << "[RECORDER] Connection error: " << msg->errorInfo.reason
                  << " (retries: " << msg->errorInfo.retries << ", wait: " << msg->errorInfo.wait_time << "ms)" << std::endl;
    } else if (msg->type == ix::WebSocketMessageType::Message) {
        uint64_t recv_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );

        if (out_file_.is_open()) {
            write_entry(recv_ns, msg->str);
        }

        if (on_message_) {
            on_message_(msg->str, recv_ns);
        }

        uint64_t count = message_count_.fetch_add(1) + 1;
        if (count % 100 == 0) {
            std::cout << "[RECORDER] Captured " << count << " messages..." << std::endl;
        }
    }
}

void RawRecorder::write_entry(uint64_t recv_time_ns, const std::string& raw_payload) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (!out_file_.is_open()) return;

    // Strip any trailing whitespace/newlines from the WebSocket message
    std::string_view payload(raw_payload);
    while (!payload.empty() && (payload.back() == '\r' || payload.back() == '\n' || payload.back() == ' ')) {
        payload.remove_suffix(1);
    }

    // Fast JSONL line output: {"recv_time_ns":<ns>,"raw":<raw_payload>}
    out_file_ << "{\"recv_time_ns\":" << recv_time_ns << ",\"raw\":" << payload << "}\n";

    // Periodic flush to prevent data loss on unexpected termination
    if (message_count_ % 50 == 0) {
        out_file_.flush();
    }
}

void RawRecorder::start() {
    ix::initNetSystem();
    if (running_.exchange(true)) return;
    std::cout << "[RECORDER] Starting WebSocket connection..." << std::endl;
    web_socket_.start();
}

void RawRecorder::stop() {
    if (!running_.exchange(false)) return;
    std::cout << "[RECORDER] Stopping recorder..." << std::endl;
    web_socket_.stop();
    connected_.store(false);

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (out_file_.is_open()) {
        out_file_.flush();
        out_file_.close();
    }
}

} // namespace polymarket
