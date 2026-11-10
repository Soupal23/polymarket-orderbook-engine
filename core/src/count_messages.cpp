#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <iomanip>
#include <cstdint>

// Lightweight string search helper for event_type without heavy JSON parsing
std::string extract_event_type(const std::string& line) {
    // Look for "event_type":"..."
    size_t key_pos = line.find("\"event_type\"");
    if (key_pos != std::string::npos) {
        size_t colon_pos = line.find(':', key_pos);
        if (colon_pos != std::string::npos) {
            size_t start_quote = line.find('"', colon_pos);
            if (start_quote != std::string::npos) {
                size_t end_quote = line.find('"', start_quote + 1);
                if (end_quote != std::string::npos) {
                    return line.substr(start_quote + 1, end_quote - start_quote - 1);
                }
            }
        }
    }

    // Secondary check for "type":"..."
    key_pos = line.find("\"type\"");
    if (key_pos != std::string::npos) {
        size_t colon_pos = line.find(':', key_pos);
        if (colon_pos != std::string::npos) {
            size_t start_quote = line.find('"', colon_pos);
            if (start_quote != std::string::npos) {
                size_t end_quote = line.find('"', start_quote + 1);
                if (end_quote != std::string::npos) {
                    return line.substr(start_quote + 1, end_quote - start_quote - 1);
                }
            }
        }
    }

    return "unknown";
}

int64_t extract_recv_time_ns(const std::string& line) {
    size_t key_pos = line.find("\"recv_time_ns\"");
    if (key_pos == std::string::npos) return 0;
    size_t colon_pos = line.find(':', key_pos);
    if (colon_pos == std::string::npos) return 0;
    size_t start_digit = line.find_first_of("0123456789", colon_pos);
    if (start_digit == std::string::npos) return 0;
    size_t end_digit = line.find_first_not_of("0123456789", start_digit);
    std::string num_str = (end_digit == std::string::npos) 
        ? line.substr(start_digit) 
        : line.substr(start_digit, end_digit - start_digit);
    try {
        return std::stoll(num_str);
    } catch (...) {
        return 0;
    }
}

int main(int argc, char* argv[]) {
    std::string filepath = "data/sample.jsonl";
    if (argc > 1) {
        filepath = argv[1];
    }

    if (!std::filesystem::exists(filepath)) {
        std::cerr << "Error: File not found: " << filepath << std::endl;
        return 1;
    }

    auto file_size = std::filesystem::file_size(filepath);
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open file: " << filepath << std::endl;
        return 1;
    }

    std::string line;
    uint64_t total_messages = 0;
    std::unordered_map<std::string, uint64_t> type_counts;
    int64_t first_ts_ns = 0;
    int64_t last_ts_ns = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        total_messages++;

        int64_t ts = extract_recv_time_ns(line);
        if (ts > 0) {
            if (first_ts_ns == 0 || ts < first_ts_ns) first_ts_ns = ts;
            if (ts > last_ts_ns) last_ts_ns = ts;
        }

        std::string type = extract_event_type(line);
        type_counts[type]++;
    }

    double elapsed_sec = 0.0;
    if (first_ts_ns > 0 && last_ts_ns > first_ts_ns) {
        elapsed_sec = static_cast<double>(last_ts_ns - first_ts_ns) / 1e9;
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "Feed Log Analysis Report" << std::endl;
    std::cout << "File: " << filepath << std::endl;
    std::cout << "Size: " << file_size << " bytes (" << std::fixed << std::setprecision(2) << (file_size / 1024.0 / 1024.0) << " MB)" << std::endl;
    std::cout << "Total Messages: " << total_messages << std::endl;
    if (elapsed_sec > 0.0) {
        std::cout << "Recorded Duration: " << std::fixed << std::setprecision(2) << elapsed_sec << " s ("
                  << std::setprecision(2) << (elapsed_sec / 60.0) << " min)" << std::endl;
        std::cout << "Average Rate: " << std::fixed << std::setprecision(2) << (total_messages / elapsed_sec) << " msgs/sec" << std::endl;
    }
    std::cout << "----------------------------------------------------------" << std::endl;
    std::cout << "Message Counts by Event Type:" << std::endl;
    for (const auto& [type, count] : type_counts) {
        double pct = (total_messages > 0) ? (100.0 * count / total_messages) : 0.0;
        std::cout << "  - " << std::left << std::setw(20) << type 
                  << ": " << std::right << std::setw(8) << count 
                  << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    return 0;
}
