#include "tcp_publisher.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>

#ifndef _WIN32
#include <netinet/tcp.h>
#include <fcntl.h>
#endif

namespace polymarket {

TcpPublisher::TcpPublisher(int port)
    : port_(port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

TcpPublisher::~TcpPublisher() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool TcpPublisher::start() {
    if (running_.load()) {
        return true;
    }

    server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_ == INVALID_SOCKET) {
        std::cerr << "[TcpPublisher] Failed to create socket\n";
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[TcpPublisher] Failed to bind to port " << port_ << "\n";
#ifdef _WIN32
        closesocket(server_socket_);
#else
        close(server_socket_);
#endif
        server_socket_ = INVALID_SOCKET;
        return false;
    }

    if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[TcpPublisher] Failed to listen on socket\n";
#ifdef _WIN32
        closesocket(server_socket_);
#else
        close(server_socket_);
#endif
        server_socket_ = INVALID_SOCKET;
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread(&TcpPublisher::accept_loop, this);
    std::cout << "[TcpPublisher] Server listening on port " << port_ << " for gateway connections\n";
    return true;
}

void TcpPublisher::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (server_socket_ != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(server_socket_);
#else
        close(server_socket_);
#endif
        server_socket_ = INVALID_SOCKET;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (socket_t client : clients_) {
#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
    }
    clients_.clear();
}

void TcpPublisher::accept_loop() {
    while (running_.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket_, &read_fds);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000; // 200ms timeout for graceful shutdown check

        int activity = select(static_cast<int>(server_socket_ + 1), &read_fds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            if (!running_.load()) break;
            continue;
        }

        if (activity > 0 && FD_ISSET(server_socket_, &read_fds)) {
            sockaddr_in client_addr{};
            int addr_len = sizeof(client_addr);
            socket_t client_sock = accept(server_socket_, reinterpret_cast<sockaddr*>(&client_addr),
#ifdef _WIN32
                &addr_len
#else
                reinterpret_cast<socklen_t*>(&addr_len)
#endif
            );

            if (client_sock != INVALID_SOCKET) {
                // Disable Nagle's algorithm for sub-millisecond IPC delivery
                int flag = 1;
#ifdef _WIN32
                setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
                setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.push_back(client_sock);
                }
                std::cout << "[TcpPublisher] Accepted new gateway client (total active: " << client_count() << ")\n";
            }
        }
    }
}

void TcpPublisher::broadcast(const std::string& json_line) {
    std::string payload = json_line;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    if (clients_.empty()) {
        return;
    }

    std::vector<socket_t> active_clients;
    active_clients.reserve(clients_.size());

    for (socket_t client : clients_) {
        int bytes_sent = send(client, payload.data(), static_cast<int>(payload.size()), 0);
        if (bytes_sent == SOCKET_ERROR || bytes_sent == 0) {
#ifdef _WIN32
            closesocket(client);
#else
            close(client);
#endif
        } else {
            active_clients.push_back(client);
        }
    }

    clients_ = std::move(active_clients);
}

std::string TcpPublisher::format_book_message(const OrderBook& book, const std::string& event_type) {
    using json = nlohmann::json;
    json j;
    j["event"] = event_type;
    j["asset_id"] = book.get_asset_id();
    j["market_id"] = book.get_market_id();
    j["state"] = to_string(book.get_state());
    j["stale_reason"] = book.get_stale_reason();
    j["timestamp_ms"] = book.get_last_timestamp_ms();

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    auto sp = book.spread();
    auto mp = book.mid_price();
    auto u_p = book.micro_price();
    auto imb = book.book_imbalance(5);

    if (bb) j["best_bid"] = *bb; else j["best_bid"] = nullptr;
    if (ba) j["best_ask"] = *ba; else j["best_ask"] = nullptr;
    if (sp) j["spread"] = *sp; else j["spread"] = nullptr;
    if (mp) j["mid_price"] = *mp; else j["mid_price"] = nullptr;
    if (u_p) j["micro_price"] = *u_p; else j["micro_price"] = nullptr;
    if (imb) j["imbalance"] = *imb; else j["imbalance"] = nullptr;

    j["total_bid_qty"] = static_cast<double>(book.total_bid_quantity_units()) / static_cast<double>(QTY_SCALE);
    j["total_ask_qty"] = static_cast<double>(book.total_ask_quantity_units()) / static_cast<double>(QTY_SCALE);

    auto depth = book.top_n_depth(10);
    json bids_arr = json::array();
    for (const auto& level : depth.bids) {
        bids_arr.push_back({level.first, level.second});
    }
    json asks_arr = json::array();
    for (const auto& level : depth.asks) {
        asks_arr.push_back({level.first, level.second});
    }

    j["bids"] = bids_arr;
    j["asks"] = asks_arr;

    return j.dump();
}

} // namespace polymarket
