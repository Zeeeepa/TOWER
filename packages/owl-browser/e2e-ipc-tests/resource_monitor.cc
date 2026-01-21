#include "resource_monitor.h"
#include <chrono>
#include <fstream>
#include <sstream>

#ifdef OS_MACOS
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#include <libproc.h>
#include <sys/proc_info.h>
#endif

#ifdef OS_LINUX
#include <unistd.h>
#endif

ResourceMonitor::ResourceMonitor(pid_t target_pid)
    : target_pid_(target_pid) {}

ResourceMonitor::~ResourceMonitor() {
    Stop();
}

void ResourceMonitor::Start(int sample_interval_ms) {
    if (running_) return;

    sample_interval_ms_ = sample_interval_ms;
    running_ = true;
    monitor_thread_ = std::thread(&ResourceMonitor::MonitorLoop, this);
}

void ResourceMonitor::Stop() {
    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void ResourceMonitor::MonitorLoop() {
    while (running_) {
        ProcessMetrics metrics = ReadProcessMetrics();

        {
            std::lock_guard<std::mutex> lock(samples_mutex_);
            samples_.push_back(metrics);

            // Update peaks
            if (metrics.rss_bytes > peak_memory_) {
                peak_memory_ = metrics.rss_bytes;
            }
            if (metrics.cpu_percent > peak_cpu_) {
                peak_cpu_ = metrics.cpu_percent;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
    }
}

ProcessMetrics ResourceMonitor::ReadProcessMetrics() {
#ifdef OS_MACOS
    return ReadMachTaskInfo();
#else
    return ReadProcStat();
#endif
}

#ifdef OS_MACOS
ProcessMetrics ResourceMonitor::ReadMachTaskInfo() {
    ProcessMetrics metrics;

    auto now = std::chrono::steady_clock::now();
    metrics.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Get memory info using proc_pid_rusage
    struct rusage_info_v4 rusage;
    if (proc_pid_rusage(target_pid_, RUSAGE_INFO_V4, (rusage_info_t*)&rusage) == 0) {
        metrics.rss_bytes = rusage.ri_resident_size;
        metrics.vms_bytes = rusage.ri_phys_footprint;
        metrics.cpu_user_time_sec = static_cast<double>(rusage.ri_user_time) / 1e9;
        metrics.cpu_system_time_sec = static_cast<double>(rusage.ri_system_time) / 1e9;
    }

    // Calculate CPU percentage (simplified)
    static double last_cpu_time = 0.0;
    static auto last_sample_time = std::chrono::steady_clock::now();

    double total_cpu = metrics.cpu_user_time_sec + metrics.cpu_system_time_sec;
    auto sample_duration = std::chrono::duration<double>(now - last_sample_time).count();

    if (sample_duration > 0 && last_cpu_time > 0) {
        double cpu_delta = total_cpu - last_cpu_time;
        metrics.cpu_percent = (cpu_delta / sample_duration) * 100.0;
    }

    last_cpu_time = total_cpu;
    last_sample_time = now;

    return metrics;
}
#else
ProcessMetrics ResourceMonitor::ReadProcStat() {
    ProcessMetrics metrics;

    auto now = std::chrono::steady_clock::now();
    metrics.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Read /proc/[pid]/stat
    std::string stat_path = "/proc/" + std::to_string(target_pid_) + "/stat";
    std::ifstream stat_file(stat_path);

    if (stat_file.is_open()) {
        std::string line;
        std::getline(stat_file, line);
        std::istringstream iss(line);

        // Skip fields until we get to the ones we need
        std::string token;
        for (int i = 0; i < 23; i++) {
            iss >> token;
            if (i == 13) {  // utime
                metrics.cpu_user_time_sec = std::stod(token) / sysconf(_SC_CLK_TCK);
            } else if (i == 14) {  // stime
                metrics.cpu_system_time_sec = std::stod(token) / sysconf(_SC_CLK_TCK);
            } else if (i == 22) {  // vsize
                metrics.vms_bytes = std::stoll(token);
            }
        }
        // Field 23 is rss in pages
        long rss_pages;
        iss >> rss_pages;
        metrics.rss_bytes = rss_pages * sysconf(_SC_PAGESIZE);
    }

    // Read /proc/[pid]/statm for more accurate memory
    std::string statm_path = "/proc/" + std::to_string(target_pid_) + "/statm";
    std::ifstream statm_file(statm_path);

    if (statm_file.is_open()) {
        long size, resident;
        statm_file >> size >> resident;
        metrics.rss_bytes = resident * sysconf(_SC_PAGESIZE);
        metrics.vms_bytes = size * sysconf(_SC_PAGESIZE);
    }

    // Calculate CPU percentage
    static double last_cpu_time = 0.0;
    static auto last_sample_time = std::chrono::steady_clock::now();

    double total_cpu = metrics.cpu_user_time_sec + metrics.cpu_system_time_sec;
    auto sample_duration = std::chrono::duration<double>(now - last_sample_time).count();

    if (sample_duration > 0 && last_cpu_time > 0) {
        double cpu_delta = total_cpu - last_cpu_time;
        metrics.cpu_percent = (cpu_delta / sample_duration) * 100.0;
    }

    last_cpu_time = total_cpu;
    last_sample_time = now;

    return metrics;
}
#endif

ProcessMetrics ResourceMonitor::GetCurrentMetrics() {
    return ReadProcessMetrics();
}

std::vector<ProcessMetrics> ResourceMonitor::GetAllSamples() {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    return samples_;
}

ProcessMetrics ResourceMonitor::GetPeakMetrics() {
    ProcessMetrics peak;
    peak.rss_bytes = peak_memory_;
    peak.cpu_percent = peak_cpu_;
    return peak;
}

ProcessMetrics ResourceMonitor::GetAverageMetrics() {
    std::lock_guard<std::mutex> lock(samples_mutex_);

    ProcessMetrics avg;
    if (samples_.empty()) return avg;

    int64_t total_memory = 0;
    double total_cpu = 0.0;

    for (const auto& sample : samples_) {
        total_memory += sample.rss_bytes;
        total_cpu += sample.cpu_percent;
    }

    avg.rss_bytes = total_memory / samples_.size();
    avg.cpu_percent = total_cpu / samples_.size();

    return avg;
}

int64_t ResourceMonitor::GetCurrentMemoryBytes() {
    return ReadProcessMetrics().rss_bytes;
}
