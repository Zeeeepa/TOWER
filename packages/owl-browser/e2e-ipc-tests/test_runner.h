#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "json.hpp"
#include "ipc_client.h"
#include "response_validator.h"
#include "resource_monitor.h"
#include "benchmark_stats.h"

using json = nlohmann::json;

// Test mode enumeration
enum class TestMode {
    SMOKE,      // Critical path only
    FULL,       // All 135 methods
    BENCHMARK,  // Performance testing
    STRESS,     // Load testing
    LEAK_CHECK  // Memory leak detection
};

class TestRunner {
public:
    TestRunner(IPCClient& client, ResourceMonitor* monitor = nullptr);

    // Run a test expecting success
    TestResult Test(const std::string& method, const json& params = json::object(),
                    const std::string& category = "", double expected_latency_ms = 0.0);

    // Run a test expecting a specific ActionStatus
    TestResult TestExpectStatus(const std::string& method, const std::string& expected_status,
                                const json& params = json::object(), const std::string& category = "");

    // Run a test expecting an error response
    TestResult TestExpectError(const std::string& method, const json& params = json::object(),
                               const std::string& category = "");

    // Run a test expecting a specific response type
    TestResult TestExpectType(const std::string& method, const std::string& expected_type,
                              const json& params = json::object(), const std::string& category = "");

    // Run a test with custom validation
    using ValidationFn = std::function<bool(const json&)>;
    TestResult TestWithValidator(const std::string& method, ValidationFn validator,
                                 const json& params = json::object(), const std::string& category = "");

    // Get results
    const std::vector<TestResult>& GetResults() const { return results_; }
    std::vector<TestResult> GetFailures() const;

    // Get statistics
    BenchmarkStats CalculateStats() const;
    std::map<std::string, CategoryStats> GetCategoryStats() const;

    // Print summary
    bool PrintSummary();

    // Verbose mode
    void SetVerbose(bool verbose) { verbose_ = verbose; }

    // Reset results
    void Reset();

    // Helper to get active context ID
    const std::string& GetActiveContext() const { return active_context_; }
    void SetActiveContext(const std::string& ctx) { active_context_ = ctx; }

private:
    IPCClient& client_;
    ResourceMonitor* monitor_;
    std::vector<TestResult> results_;
    bool verbose_ = false;
    std::string active_context_;

    TestResult ExecuteTest(const std::string& method, const json& params,
                           const std::string& category);
    void RecordResult(TestResult& result, bool passed, const std::string& error = "");
};
