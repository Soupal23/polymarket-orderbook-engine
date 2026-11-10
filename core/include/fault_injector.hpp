#pragma once

#include <string>
#include <vector>
#include <random>
#include <cstdint>

namespace polymarket {

struct FeedMessage {
    uint64_t recv_time_ns{0};
    std::string raw_json;
};

struct FaultConfig {
    double drop_rate = 0.05;      // 5% packet drop probability
    double duplicate_rate = 0.05; // 5% duplicate delivery probability
    double reorder_rate = 0.05;   // 5% out-of-order reordering probability
    double delay_rate = 0.05;     // 5% delay / timestamp regression probability
    uint64_t seed = 42;           // Deterministic pseudo-random seed
};

struct FaultStats {
    uint64_t original_count{0};
    uint64_t output_count{0};
    uint64_t drops{0};
    uint64_t duplicates{0};
    uint64_t reorders{0};
    uint64_t delays{0};
};

class FaultInjector {
public:
    static std::pair<std::vector<FeedMessage>, FaultStats> inject(
        const std::vector<FeedMessage>& input_stream,
        const FaultConfig& config
    );
};

} // namespace polymarket
