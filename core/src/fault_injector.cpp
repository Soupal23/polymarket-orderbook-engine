#include "fault_injector.hpp"
#include <random>
#include <deque>

namespace polymarket {

std::pair<std::vector<FeedMessage>, FaultStats> FaultInjector::inject(
    const std::vector<FeedMessage>& input_stream,
    const FaultConfig& config
) {
    FaultStats stats;
    stats.original_count = input_stream.size();

    std::vector<FeedMessage> output;
    output.reserve(input_stream.size() * 2);

    std::mt19937_64 rng(config.seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    std::deque<FeedMessage> delay_queue;

    for (size_t i = 0; i < input_stream.size(); ++i) {
        // Flush any held delayed message if queue is full
        if (!delay_queue.empty() && delay_queue.size() >= 3) {
            output.push_back(delay_queue.front());
            delay_queue.pop_front();
        }

        double r = dist(rng);

        // 1. Packet Drop Fault
        if (r < config.drop_rate) {
            stats.drops++;
            continue; // Dropped packet
        }

        // 2. Duplicate Packet Fault
        if (r < config.drop_rate + config.duplicate_rate) {
            stats.duplicates++;
            output.push_back(input_stream[i]);
            output.push_back(input_stream[i]); // Re-emit identical packet
            continue;
        }

        // 3. Reorder Fault (Swap with next message)
        if (r < config.drop_rate + config.duplicate_rate + config.reorder_rate) {
            if (i + 1 < input_stream.size()) {
                stats.reorders++;
                output.push_back(input_stream[i + 1]);
                output.push_back(input_stream[i]);
                ++i; // Skip next since it was swapped forward
                continue;
            }
        }

        // 4. Delay / Out-of-order Queue Fault
        if (r < config.drop_rate + config.duplicate_rate + config.reorder_rate + config.delay_rate) {
            stats.delays++;
            FeedMessage delayed = input_stream[i];
            // Regress timestamp by 100ms to test timestamp regression detector
            if (delayed.recv_time_ns > 100000000ULL) {
                delayed.recv_time_ns -= 100000000ULL;
            }
            delay_queue.push_back(delayed);
            continue;
        }

        // Normal Delivery
        output.push_back(input_stream[i]);
    }

    // Drain any remaining delayed packets
    while (!delay_queue.empty()) {
        output.push_back(delay_queue.front());
        delay_queue.pop_front();
    }

    stats.output_count = output.size();
    return {output, stats};
}

} // namespace polymarket
