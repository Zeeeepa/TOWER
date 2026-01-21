#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "benchmark_stats.h"

class ResourceMonitor {
public:
    explicit ResourceMonitor(pid_t target_pid);
    ~ResourceMonitor();

    // Start/stop monitoring
    void Start(int sample_interval_ms = 100);
    void Stop();
    bool IsRunning() const { return running_; }

    // Get metrics
    ProcessMetrics GetCurrentMetrics();
    std::vector<ProcessMetrics> GetAllSamples();
    ProcessMetrics GetPeakMetrics();
    ProcessMetrics GetAverageMetrics();

    // Get current memory (convenience)
    int64_t GetCurrentMemoryBytes();

private:
    pid_t target_pid_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
    std::vector<ProcessMetrics> samples_;
    std::mutex samples_mutex_;
    int sample_interval_ms_ = 100;

    // Peak tracking
    int64_t peak_memory_ = 0;
    double peak_cpu_ = 0.0;

    void MonitorLoop();
    ProcessMetrics ReadProcessMetrics();

#ifdef OS_MACOS
    ProcessMetrics ReadMachTaskInfo();
#else
    ProcessMetrics ReadProcStat();
#endif
};
