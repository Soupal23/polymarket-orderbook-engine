#pragma once

#include "orderbook.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace polymarket {

class TcpPublisher {
public:
    explicit TcpPublisher(int port = 9001);
    ~TcpPublisher();

    bool start();
    void stop();

    // Broadcasts formatted JSON line to all connected gateway clients
    void broadcast(const std::string& json_line);

    // Formats orderbook state into framed JSON message
    static std::string format_book_message(const OrderBook& book, const std::string& event_type = "book_update");

    size_t client_count() const {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_.size();
    }

private:
    void accept_loop();

    int port_;
    socket_t server_socket_{INVALID_SOCKET};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<socket_t> clients_;
};

} // namespace polymarket
