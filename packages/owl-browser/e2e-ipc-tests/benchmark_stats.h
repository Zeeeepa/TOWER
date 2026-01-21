#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "json.hpp"

using json = nlohmann::json;

// Per-command timing metrics
struct CommandMetrics {
    std::string method;
    double latency_ms = 0.0;           // Time from send to response received
    double parse_time_ms = 0.0;        // JSON parse time
    int64_t request_size_bytes = 0;    // Size of JSON request
    int64_t response_size_bytes = 0;   // Size of JSON response
    bool success = false;
    std::string status;                // ActionStatus code if applicable
    std::string error_message;
    int64_t memory_before_bytes = 0;
    int64_t memory_after_bytes = 0;

    json to_json() const {
        return {
            {"method", method},
            {"latency_ms", latency_ms},
            {"parse_time_ms", parse_time_ms},
            {"request_size_bytes", request_size_bytes},
            {"response_size_bytes", response_size_bytes},
            {"success", success},
            {"status", status},
            {"error_message", error_message},
            {"memory_before_mb", memory_before_bytes / (1024.0 * 1024.0)},
            {"memory_after_mb", memory_after_bytes / (1024.0 * 1024.0)}
        };
    }
};

// Process resource usage snapshot
struct ProcessMetrics {
    // Memory
    int64_t rss_bytes = 0;           // Resident Set Size
    int64_t vms_bytes = 0;           // Virtual Memory Size

    // CPU
    double cpu_user_time_sec = 0.0;    // User CPU time
    double cpu_system_time_sec = 0.0;  // System CPU time
    double cpu_percent = 0.0;          // CPU percentage

    // Timing
    int64_t timestamp_ms = 0;        // When sample was taken

    json to_json() const {
        return {
            {"timestamp_ms", timestamp_ms},
            {"memory_mb", rss_bytes / (1024.0 * 1024.0)},
            {"vms_mb", vms_bytes / (1024.0 * 1024.0)},
            {"cpu_percent", cpu_percent},
            {"cpu_user_sec", cpu_user_time_sec},
            {"cpu_system_sec", cpu_system_time_sec}
        };
    }
};

// Aggregated statistics
struct BenchmarkStats {
    // Latency stats (in milliseconds)
    double min_latency = 0.0;
    double max_latency = 0.0;
    double avg_latency = 0.0;
    double median_latency = 0.0;
    double p95_latency = 0.0;          // 95th percentile
    double p99_latency = 0.0;          // 99th percentile
    double stddev_latency = 0.0;

    // Throughput
    double commands_per_second = 0.0;
    double bytes_per_second = 0.0;

    // Resource peaks
    int64_t peak_memory_bytes = 0;
    double peak_cpu_percent = 0.0;
    int64_t avg_memory_bytes = 0;
    double avg_cpu_percent = 0.0;

    // Totals
    int total_commands = 0;
    int successful_commands = 0;
    int failed_commands = 0;
    double total_duration_sec = 0.0;

    json to_json() const {
        return {
            {"min_ms", min_latency},
            {"max_ms", max_latency},
            {"avg_ms", avg_latency},
            {"median_ms", median_latency},
            {"p95_ms", p95_latency},
            {"p99_ms", p99_latency},
            {"stddev_ms", stddev_latency}
        };
    }
};

// Category statistics
struct CategoryStats {
    std::string name;
    int total = 0;
    int passed = 0;
    int failed = 0;
    double avg_latency_ms = 0.0;
    std::vector<double> latencies;

    json to_json() const {
        return {
            {"total", total},
            {"passed", passed},
            {"failed", failed},
            {"avg_latency_ms", avg_latency_ms}
        };
    }
};

// Test result structure
struct TestResult {
    std::string method;
    std::string category;
    bool success = false;
    double duration_ms = 0.0;
    json request;
    json response;
    std::string error;
    std::string expected_status;
    std::string actual_status;
    CommandMetrics metrics;
    double expected_latency_ms = 0.0;  // Expected max latency (0 = not specified)
};

// Test failure details
struct TestFailure {
    std::string method;
    json params;
    std::string expected;
    std::string actual;
    std::string message;

    json to_json() const {
        return {
            {"method", method},
            {"params", params},
            {"expected", expected},
            {"actual", actual},
            {"message", message}
        };
    }
};
