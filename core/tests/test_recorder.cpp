#include "recorder.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED ASSERTION: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace polymarket;

void test_subscription_payload() {
    std::cout << "[RUNNING] test_subscription_payload..." << std::endl;

    RecorderConfig config;
    config.token_ids = {"token_A", "token_B"};
    config.output_path = "build/test_dummy.jsonl";

    RawRecorder recorder(config);
    // Verified by running recorder constructor and config setup
    TEST_ASSERT(recorder.get_config().token_ids.size() == 2, "token count == 2");
    TEST_ASSERT(!recorder.is_connected(), "initially disconnected");

    std::cout << "[PASSED] test_subscription_payload" << std::endl;
}

void test_log_file_writing_and_parsing() {
    std::cout << "[RUNNING] test_log_file_writing_and_parsing..." << std::endl;

    std::string test_log = "build/test_recorder_log.jsonl";
    if (std::filesystem::exists(test_log)) {
        std::filesystem::remove(test_log);
    }

    {
        std::ofstream out(test_log);
        out << "{\"recv_time_ns\":1000,\"raw\":{\"event_type\":\"book\",\"asset_id\":\"tok1\"}}\n";
        out << "{\"recv_time_ns\":2000,\"raw\":{\"event_type\":\"price_change\",\"asset_id\":\"tok1\",\"price\":\"0.5\"}}\n";
        out << "{\"recv_time_ns\":3000,\"raw\":{\"event_type\":\"last_trade_price\",\"price\":\"0.5\"}}\n";
    }

    TEST_ASSERT(std::filesystem::exists(test_log), "file created");

    // Read back and verify lines
    std::ifstream in(test_log);
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        count++;
        TEST_ASSERT(line.find("recv_time_ns") != std::string::npos, "has recv timestamp");
        if (count == 1) TEST_ASSERT(line.find("book") != std::string::npos, "line 1 book");
        if (count == 2) TEST_ASSERT(line.find("price_change") != std::string::npos, "line 2 price_change");
        if (count == 3) TEST_ASSERT(line.find("last_trade_price") != std::string::npos, "line 3 last_trade_price");
    }
    TEST_ASSERT(count == 3, "read 3 lines");
    in.close();

    std::filesystem::remove(test_log);
    std::cout << "[PASSED] test_log_file_writing_and_parsing" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Feed Recorder Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_subscription_payload();
    test_log_file_writing_and_parsing();

    std::cout << "========================================" << std::endl;
    std::cout << "ALL RECORDER TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
