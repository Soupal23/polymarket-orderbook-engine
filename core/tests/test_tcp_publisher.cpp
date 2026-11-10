#include "tcp_publisher.hpp"
#include "orderbook.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#endif

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

using namespace polymarket;
using json = nlohmann::json;

int main() {
    std::cout << "[Test] Running TCP Publisher unit tests...\n";

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    const int test_port = 9876;
    TcpPublisher publisher(test_port);
    TEST_ASSERT(publisher.start(), "Failed to start TcpPublisher");

    // Give server thread a moment to bind & listen
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect a test client socket
    socket_t client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    TEST_ASSERT(client_sock != INVALID_SOCKET, "Failed to create client socket");

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    int conn_res = connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    TEST_ASSERT(conn_res == 0, "Failed to connect client to publisher");

    // Wait for accept thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(publisher.client_count() == 1, "Expected 1 active client");

    // Prepare a test orderbook and publish state
    OrderBook book("asset_123", "market_456");
    book.apply_snapshot(
        {{4800, 100000}, {4700, 50000}}, // Bids: 0.48, 0.47
        {{5200, 80000}, {5300, 40000}},  // Asks: 0.52, 0.53
        "hash_abc",
        1720000000000ULL
    );

    std::string framed_json = TcpPublisher::format_book_message(book, "snapshot");
    publisher.broadcast(framed_json);

    // Read payload from client socket
    char buffer[2048] = {0};
    int bytes_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    TEST_ASSERT(bytes_read > 0, "Client failed to receive data from publisher");

    std::string received_str(buffer, bytes_read);
    TEST_ASSERT(received_str.back() == '\n', "Payload must be newline terminated");

    json parsed = json::parse(received_str);
    TEST_ASSERT(parsed["event"] == "snapshot", "Mismatched event type");
    TEST_ASSERT(parsed["asset_id"] == "asset_123", "Mismatched asset_id");
    TEST_ASSERT(parsed["market_id"] == "market_456", "Mismatched market_id");
    TEST_ASSERT(parsed["state"] == "LIVE", "Book state should be LIVE");
    TEST_ASSERT(std::abs(parsed["best_bid"].get<double>() - 0.48) < 1e-4, "Incorrect best bid");
    TEST_ASSERT(std::abs(parsed["best_ask"].get<double>() - 0.52) < 1e-4, "Incorrect best ask");
    TEST_ASSERT(std::abs(parsed["spread"].get<double>() - 0.04) < 1e-4, "Incorrect spread");
    TEST_ASSERT(std::abs(parsed["mid_price"].get<double>() - 0.50) < 1e-4, "Incorrect mid price");
    TEST_ASSERT(parsed["bids"].size() == 2, "Bids depth should have 2 levels");
    TEST_ASSERT(parsed["asks"].size() == 2, "Asks depth should have 2 levels");

    // Clean teardown
    closesocket(client_sock);
    publisher.stop();

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "[Test] TCP Publisher unit tests PASSED.\n";
    return 0;
}
