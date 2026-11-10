#include "orderbook.hpp"
#include "direct_array_orderbook.hpp"
#include "fast_matching_book.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace polymarket;

struct ParsedDelta {
    std::string asset_id;
    Side side;
    int64_t price_ticks;
    int64_t size_units;
};

// Pre-load all deltas into contiguous RAM memory so disk I/O and JSON parsing are excluded from CPU latency
std::vector<ParsedDelta> load_in_memory_deltas(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return {};

    std::vector<ParsedDelta> deltas;
    deltas.reserve(20000);

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            if (!j.contains("raw")) continue;
            auto& raw = j["raw"];
            if (raw.is_object() && raw.value("event_type", "") == "price_change") {
                if (raw.contains("price_changes") && raw["price_changes"].is_array()) {
                    for (auto& c : raw["price_changes"]) {
                        if (!c.contains("asset_id")) continue;
                        std::string aid = c["asset_id"].get<std::string>();
                        std::string p_str = c["price"].get<std::string>();
                        std::string s_str = c["size"].get<std::string>();
                        std::string side_str = c["side"].get<std::string>();

                        Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                        deltas.push_back({aid, side, parse_price_str(p_str), parse_qty_str(s_str)});
                    }
                }
            }
        } catch (...) {}
    }
    return deltas;
}

struct LatencyStats {
    double mean_ns;
    double p50_ns;
    double p99_ns;
    double p999_ns;
    double throughput_ops_sec;
};

LatencyStats compute_stats(std::vector<double>& latencies_ns, double total_seconds) {
    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t n = latencies_ns.size();

    double sum = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0.0);
    double mean = sum / n;
    double p50 = latencies_ns[static_cast<size_t>(n * 0.50)];
    double p99 = latencies_ns[static_cast<size_t>(n * 0.99)];
    double p999 = latencies_ns[static_cast<size_t>(n * 0.999)];
    double throughput = n / total_seconds;

    return {mean, p50, p99, p999, throughput};
}

int main(int argc, char* argv[]) {
    std::string filepath = "data/polymarket_recording.jsonl";
    if (argc > 1) filepath = argv[1];

    std::cout << "==========================================================" << std::endl;
    std::cout << "Polymarket OrderBook Data-Structure Benchmark Harness" << std::endl;
    std::cout << "Compiler: GCC " << __VERSION__ << std::endl;
#if defined(__OPTIMIZE__) || defined(NDEBUG)
    std::cout << "Optimization: Enabled (-O2 / -O3)" << std::endl;
#else
    std::cout << "Optimization: Debug / Off" << std::endl;
#endif
    std::cout << "==========================================================" << std::endl;

    auto deltas = load_in_memory_deltas(filepath);
    if (deltas.empty()) {
        std::cerr << "Failed to load deltas from: " << filepath << std::endl;
        return 1;
    }
    std::cout << "[SETUP] Preloaded " << deltas.size() << " real updates into memory." << std::endl;

    constexpr int NUM_TRIALS = 5;

    // --- BENCHMARK 1: Baseline std::map OrderBook ---
    std::vector<double> map_throughputs;
    std::vector<LatencyStats> map_trial_stats;

    // Warmup
    {
        std::unordered_map<std::string, OrderBook> books;
        for (const auto& d : deltas) {
            books[d.asset_id].apply_delta(d.side, d.price_ticks, d.size_units);
        }
    }

    for (int t = 0; t < NUM_TRIALS; ++t) {
        std::unordered_map<std::string, OrderBook> books;
        std::vector<double> latencies;
        latencies.reserve(deltas.size());

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& d : deltas) {
            auto t0 = std::chrono::high_resolution_clock::now();
            books[d.asset_id].apply_delta(d.side, d.price_ticks, d.size_units);
            auto t1 = std::chrono::high_resolution_clock::now();
            latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(end - start).count();

        auto stats = compute_stats(latencies, elapsed_sec);
        map_trial_stats.push_back(stats);
        map_throughputs.push_back(stats.throughput_ops_sec);
    }

    // --- BENCHMARK 2: Optimized DirectArrayOrderBook ---
    std::vector<double> array_throughputs;
    std::vector<LatencyStats> array_trial_stats;

    // Warmup
    {
        std::unordered_map<std::string, DirectArrayOrderBook> books;
        for (const auto& d : deltas) {
            books[d.asset_id].apply_delta(d.side, d.price_ticks, d.size_units);
        }
    }

    for (int t = 0; t < NUM_TRIALS; ++t) {
        std::unordered_map<std::string, DirectArrayOrderBook> books;
        std::vector<double> latencies;
        latencies.reserve(deltas.size());

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& d : deltas) {
            auto t0 = std::chrono::high_resolution_clock::now();
            books[d.asset_id].apply_delta(d.side, d.price_ticks, d.size_units);
            auto t1 = std::chrono::high_resolution_clock::now();
            latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(end - start).count();

        auto stats = compute_stats(latencies, elapsed_sec);
        array_trial_stats.push_back(stats);
        array_throughputs.push_back(stats.throughput_ops_sec);
    }

    // --- BENCHMARK 3: Order Cancellation (Baseline std::deque O(K) vs FastMatchingBook O(1) DLL) ---
    constexpr int CANCEL_ORDERS = 50000;
    constexpr int LEVEL_DENSITY_K = 100; // 100 orders resting per price level

    double deque_cancel_total_ns = 0.0;
    {
        OrderBook base_book;
        // Seed book with K orders per level across multiple levels
        for (int i = 0; i < CANCEL_ORDERS; ++i) {
            int64_t price = 4000 + (i % (CANCEL_ORDERS / LEVEL_DENSITY_K)) * 10;
            base_book.add_limit_order(Order(i + 1, Side::BUY, price, 1000));
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < CANCEL_ORDERS; ++i) {
            base_book.cancel_order(i + 1);
        }
        auto end = std::chrono::high_resolution_clock::now();
        deque_cancel_total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    }

    double dll_cancel_total_ns = 0.0;
    {
        FastMatchingBook fast_book;
        for (int i = 0; i < CANCEL_ORDERS; ++i) {
            int64_t price = 4000 + (i % (CANCEL_ORDERS / LEVEL_DENSITY_K)) * 10;
            fast_book.add_limit_order(Order(i + 1, Side::BUY, price, 1000));
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < CANCEL_ORDERS; ++i) {
            fast_book.cancel_order_fast(i + 1);
        }
        auto end = std::chrono::high_resolution_clock::now();
        dll_cancel_total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    }

    double deque_avg_cancel_ns = deque_cancel_total_ns / CANCEL_ORDERS;
    double dll_avg_cancel_ns = dll_cancel_total_ns / CANCEL_ORDERS;
    double dll_cancel_throughput = CANCEL_ORDERS / (dll_cancel_total_ns / 1e9);
    double deque_cancel_throughput = CANCEL_ORDERS / (deque_cancel_total_ns / 1e9);

    // Compute averages across 5 trials for L2 feeds
    auto avg_stat = [](const std::vector<LatencyStats>& v) {
        LatencyStats res{0, 0, 0, 0, 0};
        for (const auto& s : v) {
            res.mean_ns += s.mean_ns;
            res.p50_ns += s.p50_ns;
            res.p99_ns += s.p99_ns;
            res.p999_ns += s.p999_ns;
            res.throughput_ops_sec += s.throughput_ops_sec;
        }
        double sz = static_cast<double>(v.size());
        return LatencyStats{res.mean_ns / sz, res.p50_ns / sz, res.p99_ns / sz, res.p999_ns / sz, res.throughput_ops_sec / sz};
    };

    auto map_avg = avg_stat(map_trial_stats);
    auto array_avg = avg_stat(array_trial_stats);

    // Variance calculation
    double map_mean_t = map_avg.throughput_ops_sec;
    double map_var = 0.0;
    for (double x : map_throughputs) map_var += (x - map_mean_t) * (x - map_mean_t);
    double map_std = std::sqrt(map_var / NUM_TRIALS);

    double array_mean_t = array_avg.throughput_ops_sec;
    double array_var = 0.0;
    for (double x : array_throughputs) array_var += (x - array_mean_t) * (x - array_mean_t);
    double array_std = std::sqrt(array_var / NUM_TRIALS);

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "BENCHMARK RESULTS SUMMARY (Across " << NUM_TRIALS << " Trials)" << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n[1] Baseline std::map OrderBook:" << std::endl;
    std::cout << "  Throughput: " << std::setprecision(0) << map_avg.throughput_ops_sec << " ops/sec (+/- " << map_std << ")" << std::endl;
    std::cout << "  Latency Mean: " << std::setprecision(2) << map_avg.mean_ns << " ns" << std::endl;
    std::cout << "  Latency p50:  " << map_avg.p50_ns << " ns" << std::endl;
    std::cout << "  Latency p99:  " << map_avg.p99_ns << " ns" << std::endl;
    std::cout << "  Latency p99.9:" << map_avg.p999_ns << " ns" << std::endl;

    std::cout << "\n[2] DirectArrayOrderBook (Price-Tick Flat Array):" << std::endl;
    std::cout << "  Throughput: " << std::setprecision(0) << array_avg.throughput_ops_sec << " ops/sec (+/- " << array_std << ")" << std::endl;
    std::cout << "  Latency Mean: " << std::setprecision(2) << array_avg.mean_ns << " ns" << std::endl;
    std::cout << "  Latency p50:  " << array_avg.p50_ns << " ns" << std::endl;
    std::cout << "  Latency p99:  " << array_avg.p99_ns << " ns" << std::endl;
    std::cout << "  Latency p99.9:" << array_avg.p999_ns << " ns" << std::endl;

    std::cout << "\n[3] Order Cancellation Benchmark (Density K=" << LEVEL_DENSITY_K << " orders/level):" << std::endl;
    std::cout << "  Baseline std::deque O(K) Linear Scan:  " << deque_avg_cancel_ns << " ns/cancel (" << std::setprecision(0) << deque_cancel_throughput << " cancels/sec)" << std::endl;
    std::cout << "  Optimized std::list True O(1) Pointer: " << std::setprecision(2) << dll_avg_cancel_ns << " ns/cancel (" << std::setprecision(0) << dll_cancel_throughput << " cancels/sec)" << std::endl;
    std::cout << "  Speedup: " << std::setprecision(1) << (deque_avg_cancel_ns / dll_avg_cancel_ns) << "x faster" << std::endl;

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "Speedup of DirectArray vs std::map: " << std::setprecision(2) << (array_avg.throughput_ops_sec / map_avg.throughput_ops_sec) << "x" << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
