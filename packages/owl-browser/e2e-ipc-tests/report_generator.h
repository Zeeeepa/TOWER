#pragma once

#include <string>
#include <vector>
#include <map>
#include "json.hpp"
#include "benchmark_stats.h"

using json = nlohmann::json;

struct ReportMetadata {
    std::string test_run_id;
    std::string timestamp;
    std::string test_mode;
    std::string browser_version;
    std::string browser_path;
    std::string platform;
    std::string platform_version;
    std::string cpu_model;
    double total_memory_gb;

    json to_json() const;
};

class ReportGenerator {
public:
    ReportGenerator();

    // Set report data
    void SetMetadata(const ReportMetadata& metadata);
    void SetResults(const std::vector<TestResult>& results);
    void SetResourceTimeline(const std::vector<ProcessMetrics>& timeline);
    void SetBenchmarkStats(const BenchmarkStats& stats);
    void SetCategoryStats(const std::map<std::string, CategoryStats>& categories);

    // Generate report
    json GenerateJSON() const;
    bool SaveJSON(const std::string& filepath) const;

private:
    ReportMetadata metadata_;
    std::vector<TestResult> results_;
    std::vector<ProcessMetrics> resource_timeline_;
    BenchmarkStats stats_;
    std::map<std::string, CategoryStats> category_stats_;

    json BuildSummary() const;
    json BuildFailures() const;
    json BuildCommands() const;
};
