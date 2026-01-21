#include "test_runner.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>

TestRunner::TestRunner(IPCClient& client, ResourceMonitor* monitor)
    : client_(client), monitor_(monitor) {}

TestResult TestRunner::ExecuteTest(const std::string& method, const json& params,
                                    const std::string& category) {
    TestResult result;
    result.method = method;
    result.category = category.empty() ? "uncategorized" : category;
    result.request = params;

    // Capture memory before
    if (monitor_) {
        result.metrics.memory_before_bytes = monitor_->GetCurrentMemoryBytes();
    }

    // Execute command
    auto start = std::chrono::high_resolution_clock::now();
    result.response = client_.Send(method, params);
    auto end = std::chrono::high_resolution_clock::now();

    result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Capture metrics
    result.metrics.method = method;
    result.metrics.latency_ms = client_.GetLastResponseTimeMs();
    result.metrics.parse_time_ms = client_.GetLastParseTimeMs();
    result.metrics.request_size_bytes = client_.GetLastRequestSize();
    result.metrics.response_size_bytes = client_.GetLastResponseSize();

    // Capture memory after
    if (monitor_) {
        result.metrics.memory_after_bytes = monitor_->GetCurrentMemoryBytes();
    }

    // Get status
    result.actual_status = ResponseValidator::GetStatus(result.response);
    result.metrics.status = result.actual_status;

    return result;
}

void TestRunner::RecordResult(TestResult& result, bool passed, const std::string& error) {
    result.success = passed;
    result.error = error;
    result.metrics.success = passed;
    result.metrics.error_message = error;
    results_.push_back(result);

    if (verbose_) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << result.method << " (" << std::fixed << std::setprecision(1)
                  << result.metrics.latency_ms << "ms)";
        if (!passed && !error.empty()) {
            std::cout << " - " << error;
        }
        std::cout << std::endl;
        std::cout.flush();
    }
}

TestResult TestRunner::Test(const std::string& method, const json& params,
                             const std::string& category, double expected_latency_ms) {
    TestResult result = ExecuteTest(method, params, category);
    result.expected_latency_ms = expected_latency_ms;

    bool passed = ResponseValidator::IsSuccess(result.response);
    std::string error;

    if (!passed) {
        if (ResponseValidator::IsErrorResponse(result.response)) {
            error = "Error response: " + result.response["error"].get<std::string>();
        } else if (ResponseValidator::IsActionResult(result.response)) {
            error = "Action failed with status: " + result.actual_status +
                    " - " + ResponseValidator::GetMessage(result.response);
        } else {
            error = "Unexpected failure";
        }
    }

    RecordResult(result, passed, error);
    return result;
}

TestResult TestRunner::TestExpectStatus(const std::string& method, const std::string& expected_status,
                                         const json& params, const std::string& category) {
    TestResult result = ExecuteTest(method, params, category);
    result.expected_status = expected_status;

    bool passed = ResponseValidator::HasStatus(result.response, expected_status);
    std::string error;

    if (!passed) {
        error = "Expected status '" + expected_status + "', got '" + result.actual_status + "'";
    }

    RecordResult(result, passed, error);
    return result;
}

TestResult TestRunner::TestExpectError(const std::string& method, const json& params,
                                        const std::string& category) {
    TestResult result = ExecuteTest(method, params, category);
    result.expected_status = "error";

    bool passed = ResponseValidator::IsErrorResponse(result.response);
    std::string error;

    if (!passed) {
        error = "Expected error response, got: " + ResponseValidator::GetResponseType(result.response);
    }

    RecordResult(result, passed, error);
    return result;
}

TestResult TestRunner::TestExpectType(const std::string& method, const std::string& expected_type,
                                       const json& params, const std::string& category) {
    TestResult result = ExecuteTest(method, params, category);

    std::string actual_type = ResponseValidator::GetResponseType(result.response);
    bool passed = (actual_type == expected_type);
    std::string error;

    if (!passed) {
        error = "Expected response type '" + expected_type + "', got '" + actual_type + "'";
    }
    // Note: We only check the response type, not the operation result.
    // A Boolean response of false (e.g., canGoBack when no history) is still a valid response.

    RecordResult(result, passed, error);
    return result;
}

TestResult TestRunner::TestWithValidator(const std::string& method, ValidationFn validator,
                                          const json& params, const std::string& category) {
    TestResult result = ExecuteTest(method, params, category);

    bool passed = validator(result.response);
    std::string error = passed ? "" : "Custom validation failed";

    RecordResult(result, passed, error);
    return result;
}

std::vector<TestResult> TestRunner::GetFailures() const {
    std::vector<TestResult> failures;
    for (const auto& result : results_) {
        if (!result.success) {
            failures.push_back(result);
        }
    }
    return failures;
}

BenchmarkStats TestRunner::CalculateStats() const {
    BenchmarkStats stats;

    if (results_.empty()) return stats;

    std::vector<double> latencies;
    int64_t total_bytes = 0;

    for (const auto& result : results_) {
        latencies.push_back(result.metrics.latency_ms);
        total_bytes += result.metrics.request_size_bytes + result.metrics.response_size_bytes;

        if (result.success) {
            stats.successful_commands++;
        } else {
            stats.failed_commands++;
        }
    }

    stats.total_commands = results_.size();

    // Sort latencies for percentile calculations
    std::sort(latencies.begin(), latencies.end());

    stats.min_latency = latencies.front();
    stats.max_latency = latencies.back();

    // Calculate average
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    stats.avg_latency = sum / latencies.size();

    // Median
    size_t mid = latencies.size() / 2;
    stats.median_latency = (latencies.size() % 2 == 0) ?
        (latencies[mid - 1] + latencies[mid]) / 2.0 : latencies[mid];

    // Percentiles
    stats.p95_latency = latencies[static_cast<size_t>(latencies.size() * 0.95)];
    stats.p99_latency = latencies[static_cast<size_t>(latencies.size() * 0.99)];

    // Standard deviation
    double sq_sum = 0.0;
    for (double lat : latencies) {
        sq_sum += (lat - stats.avg_latency) * (lat - stats.avg_latency);
    }
    stats.stddev_latency = std::sqrt(sq_sum / latencies.size());

    // Calculate total duration
    stats.total_duration_sec = sum / 1000.0;

    // Throughput
    if (stats.total_duration_sec > 0) {
        stats.commands_per_second = stats.total_commands / stats.total_duration_sec;
        stats.bytes_per_second = total_bytes / stats.total_duration_sec;
    }

    return stats;
}

std::map<std::string, CategoryStats> TestRunner::GetCategoryStats() const {
    std::map<std::string, CategoryStats> category_stats;

    for (const auto& result : results_) {
        auto& cat = category_stats[result.category];
        cat.name = result.category;
        cat.total++;
        if (result.success) {
            cat.passed++;
        } else {
            cat.failed++;
        }
        cat.latencies.push_back(result.metrics.latency_ms);
    }

    // Calculate averages
    for (auto& [name, cat] : category_stats) {
        if (!cat.latencies.empty()) {
            double sum = std::accumulate(cat.latencies.begin(), cat.latencies.end(), 0.0);
            cat.avg_latency_ms = sum / cat.latencies.size();
        }
    }

    return category_stats;
}

bool TestRunner::PrintSummary() {
    int passed = 0, failed = 0;

    for (const auto& result : results_) {
        if (result.success) {
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "TEST SUMMARY\n";
    std::cout << "========================================\n";
    std::cout << "Total:  " << results_.size() << "\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "========================================\n";

    if (failed > 0) {
        std::cout << "\nFAILED TESTS:\n";
        for (const auto& result : results_) {
            if (!result.success) {
                std::cout << "  - " << result.method;
                if (!result.error.empty()) {
                    std::cout << ": " << result.error;
                }
                std::cout << "\n";
            }
        }
    }

    // Print benchmark stats
    auto stats = CalculateStats();
    std::cout << "\nLATENCY STATS:\n";
    std::cout << "  Min:    " << std::fixed << std::setprecision(2) << stats.min_latency << "ms\n";
    std::cout << "  Max:    " << stats.max_latency << "ms\n";
    std::cout << "  Avg:    " << stats.avg_latency << "ms\n";
    std::cout << "  Median: " << stats.median_latency << "ms\n";
    std::cout << "  P95:    " << stats.p95_latency << "ms\n";
    std::cout << "  P99:    " << stats.p99_latency << "ms\n";
    std::cout << "  StdDev: " << stats.stddev_latency << "ms\n";
    std::cout << "\nTHROUGHPUT:\n";
    std::cout << "  Commands/sec: " << std::setprecision(1) << stats.commands_per_second << "\n";
    std::cout << "  Duration:     " << std::setprecision(2) << stats.total_duration_sec << "s\n";

    return failed == 0;
}

void TestRunner::Reset() {
    results_.clear();
    active_context_.clear();
}
