#include "report_generator.h"
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>

#ifdef OS_MACOS
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif

json ReportMetadata::to_json() const {
    return {
        {"test_run_id", test_run_id},
        {"timestamp", timestamp},
        {"test_mode", test_mode},
        {"browser_version", browser_version},
        {"browser_path", browser_path},
        {"platform", platform},
        {"platform_version", platform_version},
        {"cpu_model", cpu_model},
        {"total_memory_gb", total_memory_gb}
    };
}

ReportGenerator::ReportGenerator() {}

void ReportGenerator::SetMetadata(const ReportMetadata& metadata) {
    metadata_ = metadata;
}

void ReportGenerator::SetResults(const std::vector<TestResult>& results) {
    results_ = results;
}

void ReportGenerator::SetResourceTimeline(const std::vector<ProcessMetrics>& timeline) {
    resource_timeline_ = timeline;
}

void ReportGenerator::SetBenchmarkStats(const BenchmarkStats& stats) {
    stats_ = stats;
}

void ReportGenerator::SetCategoryStats(const std::map<std::string, CategoryStats>& categories) {
    category_stats_ = categories;
}

json ReportGenerator::BuildSummary() const {
    int passed = 0, failed = 0;
    for (const auto& r : results_) {
        if (r.success) passed++;
        else failed++;
    }

    return {
        {"total_tests", results_.size()},
        {"passed", passed},
        {"failed", failed},
        {"skipped", 0},
        {"total_duration_sec", stats_.total_duration_sec},
        {"commands_per_second", stats_.commands_per_second}
    };
}

json ReportGenerator::BuildFailures() const {
    json failures = json::array();

    for (const auto& r : results_) {
        if (!r.success) {
            failures.push_back({
                {"method", r.method},
                {"params", r.request},
                {"expected", r.expected_status.empty() ? "success" : r.expected_status},
                {"actual", r.actual_status},
                {"message", r.error}
            });
        }
    }

    return failures;
}

json ReportGenerator::BuildCommands() const {
    json commands = json::array();

    for (const auto& r : results_) {
        commands.push_back({
            {"method", r.method},
            {"category", r.category},
            {"params", r.request},
            {"success", r.success},
            {"latency_ms", r.metrics.latency_ms},
            {"expected_latency_ms", r.expected_latency_ms},
            {"response_size_bytes", r.metrics.response_size_bytes},
            {"status", r.actual_status},
            {"memory_before_mb", r.metrics.memory_before_bytes / (1024.0 * 1024.0)},
            {"memory_after_mb", r.metrics.memory_after_bytes / (1024.0 * 1024.0)}
        });
    }

    return commands;
}

json ReportGenerator::GenerateJSON() const {
    // Resource stats
    json resource_stats = json::object();
    if (!resource_timeline_.empty()) {
        int64_t peak_mem = 0, total_mem = 0;
        double peak_cpu = 0, total_cpu = 0;

        for (const auto& sample : resource_timeline_) {
            if (sample.rss_bytes > peak_mem) peak_mem = sample.rss_bytes;
            if (sample.cpu_percent > peak_cpu) peak_cpu = sample.cpu_percent;
            total_mem += sample.rss_bytes;
            total_cpu += sample.cpu_percent;
        }

        resource_stats = {
            {"peak_memory_mb", peak_mem / (1024.0 * 1024.0)},
            {"avg_memory_mb", (total_mem / resource_timeline_.size()) / (1024.0 * 1024.0)},
            {"peak_cpu_percent", peak_cpu},
            {"avg_cpu_percent", total_cpu / resource_timeline_.size()}
        };
    }

    // Category stats
    json by_category = json::object();
    for (const auto& [name, cat] : category_stats_) {
        by_category[name] = cat.to_json();
    }

    // Resource timeline
    json timeline = json::array();
    for (const auto& sample : resource_timeline_) {
        timeline.push_back(sample.to_json());
    }

    return {
        {"metadata", metadata_.to_json()},
        {"summary", BuildSummary()},
        {"latency_stats", stats_.to_json()},
        {"resource_stats", resource_stats},
        {"by_category", by_category},
        {"commands", BuildCommands()},
        {"resource_timeline", timeline},
        {"failures", BuildFailures()}
    };
}

bool ReportGenerator::SaveJSON(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << std::setw(2) << GenerateJSON() << std::endl;
    return true;
}
