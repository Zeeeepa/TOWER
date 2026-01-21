#include <iostream>
#include <string>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <uuid/uuid.h>
#include <thread>
#include <vector>
#include <atomic>

#ifdef OS_MACOS
#include <sys/utsname.h>
#include <sys/sysctl.h>
#endif

#ifdef OS_LINUX
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <fstream>
#endif

#include "ipc_client.h"
#include "test_runner.h"
#include "resource_monitor.h"
#include "report_generator.h"
#include "html_report_generator.h"
#include "method_tests.h"

// Generate UUID for test run
std::string GenerateUUID() {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    return std::string(uuid_str);
}

// Get current timestamp
std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Get platform info
std::string GetPlatform() {
#ifdef OS_MACOS
    return "darwin";
#else
    return "linux";
#endif
}

std::string GetPlatformVersion() {
#if defined(OS_MACOS) || defined(OS_LINUX)
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string(info.release);
    }
#endif
    return "unknown";
}

std::string GetCPUModel() {
#ifdef OS_MACOS
    char buffer[256];
    size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0) {
        return std::string(buffer);
    }
#endif
#ifdef OS_LINUX
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string model = line.substr(pos + 1);
                // Trim leading whitespace
                size_t start = model.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    return model.substr(start);
                }
            }
        }
    }
#endif
    return "unknown";
}

double GetTotalMemoryGB() {
#ifdef OS_MACOS
    int64_t mem_size;
    size_t size = sizeof(mem_size);
    if (sysctlbyname("hw.memsize", &mem_size, &size, nullptr, 0) == 0) {
        return mem_size / (1024.0 * 1024.0 * 1024.0);
    }
#endif
#ifdef OS_LINUX
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.totalram * info.mem_unit) / (1024.0 * 1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

// Detect browser path
std::string DetectBrowserPath() {
#ifdef OS_MACOS
    // Try relative path first (from build directory)
    const char* paths[] = {
        "../build/Release/owl_browser.app/Contents/MacOS/owl_browser",
        "build/Release/owl_browser.app/Contents/MacOS/owl_browser",
        "./Release/owl_browser.app/Contents/MacOS/owl_browser",
        nullptr
    };
#else
    const char* paths[] = {
        "../build/owl_browser",
        "build/owl_browser",
        "./owl_browser",
        nullptr
    };
#endif

    for (const char** path = paths; *path; ++path) {
        if (access(*path, X_OK) == 0) {
            return std::string(*path);
        }
    }

    return "";
}

// Convert connection mode string to enum
ConnectionMode ParseConnectionMode(const std::string& mode_str) {
    if (mode_str == "socket") return ConnectionMode::SOCKET;
    if (mode_str == "pipe") return ConnectionMode::PIPE;
    return ConnectionMode::AUTO;  // Default
}

std::string ConnectionModeToString(ConnectionMode mode) {
    switch (mode) {
        case ConnectionMode::SOCKET: return "socket";
        case ConnectionMode::PIPE: return "pipe";
        case ConnectionMode::AUTO: return "auto";
    }
    return "unknown";
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --browser-path PATH   Path to owl_browser binary\n";
    std::cout << "  --test-url URL        URL to use for testing (default: owl://user_form.html/)\n";
    std::cout << "  --mode MODE           Test mode: smoke, full, benchmark, stress, leak-check, parallel\n";
    std::cout << "  --connection-mode M   Connection mode: auto, socket, pipe (default: auto)\n";
    std::cout << "  --concurrency N       Number of parallel threads for parallel mode (default: 1)\n";
    std::cout << "  --verbose             Enable verbose output\n";
    std::cout << "  --json-report FILE    Output JSON report to file\n";
    std::cout << "  --html-report FILE    Output HTML report to file\n";
    std::cout << "  --iterations N        Number of iterations for benchmark mode\n";
    std::cout << "  --contexts N          Number of contexts for stress mode\n";
    std::cout << "  --duration N          Duration in seconds for stress/leak-check modes\n";
    std::cout << "  --help                Show this help message\n";
    std::cout << "\n";
    std::cout << "Connection Modes:\n";
    std::cout << "  auto    - Try socket first, fallback to pipe (default)\n";
    std::cout << "  socket  - Use Unix Domain Socket only (requires browser support)\n";
    std::cout << "  pipe    - Use stdin/stdout pipes only (legacy mode)\n";
    std::cout << "\n";
    std::cout << "Parallel Mode:\n";
    std::cout << "  Use --mode parallel with --concurrency N to run N parallel browser contexts.\n";
    std::cout << "  Each context runs the smoke test suite concurrently using socket connections.\n";
    std::cout << "  Socket mode is required for true parallelism.\n";
}

// Run a parallel smoke test on a single socket client
void RunParallelSmokeTest(SocketClient* client, int thread_id, const std::string& test_url,
                          std::atomic<int>& passed, std::atomic<int>& failed, bool verbose) {
    if (verbose) {
        std::cerr << "[Thread " << thread_id << "] Starting parallel smoke test" << std::endl;
    }

    // Create context
    auto ctx_result = client->Send("createContext", {});
    if (!ctx_result.contains("result") || !ctx_result["result"].is_string()) {
        if (verbose) {
            std::cerr << "[Thread " << thread_id << "] Failed to create context" << std::endl;
        }
        failed++;
        return;
    }

    std::string ctx = ctx_result["result"].get<std::string>();
    if (verbose) {
        std::cerr << "[Thread " << thread_id << "] Created context: " << ctx << std::endl;
    }

    // Navigate
    auto nav_result = client->Send("navigate", {{"context_id", ctx}, {"url", test_url}});
    if (nav_result.contains("error")) {
        failed++;
        client->Send("releaseContext", {{"context_id", ctx}});
        return;
    }

    // Wait for navigation
    auto wait_result = client->Send("waitForNavigation", {{"context_id", ctx}, {"timeout", 15000}});

    // Screenshot
    auto ss_result = client->Send("screenshot", {{"context_id", ctx}});
    if (!ss_result.contains("result") || !ss_result["result"].is_string()) {
        if (verbose) {
            std::cerr << "[Thread " << thread_id << "] Screenshot failed" << std::endl;
        }
        failed++;
        client->Send("releaseContext", {{"context_id", ctx}});
        return;
    }

    // Extract text
    auto text_result = client->Send("extractText", {{"context_id", ctx}});

    // Release context
    auto release_result = client->Send("releaseContext", {{"context_id", ctx}});

    if (verbose) {
        std::cerr << "[Thread " << thread_id << "] Completed successfully" << std::endl;
    }
    passed++;
}

int main(int argc, char* argv[]) {
    // Default options
    std::string browser_path;
    std::string test_url = "owl://user_form.html/";
    std::string mode = "full";
    std::string connection_mode_str = "auto";
    bool verbose = false;
    std::string json_report_path;
    std::string html_report_path;
    int iterations = 1;
    int contexts = 10;
    int duration = 60;
    int concurrency = 1;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--browser-path") == 0 && i + 1 < argc) {
            browser_path = argv[++i];
        } else if (strcmp(argv[i], "--test-url") == 0 && i + 1 < argc) {
            test_url = argv[++i];
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--connection-mode") == 0 && i + 1 < argc) {
            connection_mode_str = argv[++i];
        } else if (strcmp(argv[i], "--concurrency") == 0 && i + 1 < argc) {
            concurrency = std::stoi(argv[++i]);
            if (concurrency < 1) concurrency = 1;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--json-report") == 0 && i + 1 < argc) {
            json_report_path = argv[++i];
        } else if (strcmp(argv[i], "--html-report") == 0 && i + 1 < argc) {
            html_report_path = argv[++i];
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--contexts") == 0 && i + 1 < argc) {
            contexts = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            PrintUsage(argv[0]);
            return 3;
        }
    }

    // Detect browser path if not specified
    if (browser_path.empty()) {
        browser_path = DetectBrowserPath();
        if (browser_path.empty()) {
            std::cerr << "[FATAL] Could not find owl_browser binary.\n";
            std::cerr << "Use --browser-path to specify the path.\n";
            return 2;
        }
    }

    ConnectionMode connection_mode = ParseConnectionMode(connection_mode_str);

    std::cout << "========================================\n";
    std::cout << "OWL BROWSER IPC TEST CLIENT\n";
    std::cout << "========================================\n";
    std::cout << "Browser:    " << browser_path << "\n";
    std::cout << "Mode:       " << mode << "\n";
    std::cout << "Connection: " << connection_mode_str << "\n";
    if (mode == "parallel") {
        std::cout << "Concurrency: " << concurrency << " threads\n";
    }
    std::cout << "URL:        " << test_url << "\n";
    std::cout << "========================================\n\n";

    // Start browser
    std::cout << "[INFO] Starting browser..." << std::endl;
    IPCClient client(browser_path);

    std::string instance_id = "ipc_test_" + std::to_string(time(nullptr));
    if (!client.Start(instance_id, connection_mode)) {
        std::cerr << "[FATAL] Failed to start browser" << std::endl;
        return 2;
    }

    std::cout << "[INFO] Browser started (PID: " << client.GetBrowserPID() << ")" << std::endl;
    std::cout << "[INFO] Connection mode: " << ConnectionModeToString(client.GetConnectionMode());
    if (client.IsSocketMode()) {
        std::cout << " (" << client.GetSocketPath() << ")";
    }
    std::cout << std::endl;

    // Start resource monitor
    ResourceMonitor monitor(client.GetBrowserPID());
    monitor.Start(100);  // Sample every 100ms

    // Create test runner
    TestRunner runner(client, &monitor);
    runner.SetVerbose(verbose);
    client.SetVerbose(verbose);  // Enable IPC-level verbose output

    // Run tests based on mode
    bool all_passed = true;

    if (mode == "smoke") {
        std::cout << "\n[INFO] Running smoke tests...\n" << std::endl;

        // Quick critical path test
        auto ctx_result = client.Send("createContext", {});
        std::cout << "[DEBUG] createContext response: " << ctx_result.dump() << std::endl;

        if (ResponseValidator::ValidateContextId(ctx_result)) {
            std::string ctx = ResponseValidator::GetStringResult(ctx_result);
            std::cout << "[DEBUG] Got context: " << ctx << std::endl;
            runner.SetActiveContext(ctx);

            runner.Test("navigate", {{"context_id", ctx}, {"url", test_url}}, "smoke");
            runner.Test("waitForNavigation", {{"context_id", ctx}, {"timeout", 15000}}, "smoke");
            runner.Test("extractText", {{"context_id", ctx}}, "smoke");
            runner.Test("screenshot", {{"context_id", ctx}}, "smoke");
            runner.Test("releaseContext", {{"context_id", ctx}}, "smoke");
        } else {
            std::cerr << "[ERROR] Failed to create context - validation failed" << std::endl;
            std::cerr << "[DEBUG] Response type: " << ResponseValidator::GetResponseType(ctx_result) << std::endl;
        }

        all_passed = runner.PrintSummary();
    }
    else if (mode == "full") {
        all_passed = RunAllMethodTests(runner, client, test_url);
    }
    else if (mode == "parallel") {
        std::cout << "\n[INFO] Running parallel test with " << concurrency << " threads...\n" << std::endl;

        // Parallel mode requires socket connection
        if (!client.IsSocketMode()) {
            std::cerr << "[ERROR] Parallel mode requires socket connection.\n";
            std::cerr << "[INFO] Browser did not advertise socket support.\n";
            std::cerr << "[INFO] Try running with --connection-mode pipe and --concurrency 1 instead.\n";
            client.Stop();
            return 2;
        }

        std::string socket_path = client.GetSocketPath();
        std::cout << "[INFO] Using socket: " << socket_path << std::endl;

        // Create connection pool
        IPCConnectionPool pool(concurrency);
        pool.SetVerbose(verbose);

        if (!pool.Initialize(socket_path)) {
            std::cerr << "[ERROR] Failed to initialize connection pool" << std::endl;
            client.Stop();
            return 2;
        }

        std::cout << "[INFO] Connection pool initialized with " << pool.GetPoolSize() << " connections" << std::endl;

        // Run parallel smoke tests
        std::atomic<int> passed{0};
        std::atomic<int> failed{0};
        std::vector<std::thread> threads;

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < concurrency; i++) {
            threads.emplace_back([&pool, i, &test_url, &passed, &failed, verbose]() {
                SocketClient* conn = pool.AcquireConnection(10000);
                if (!conn) {
                    std::cerr << "[Thread " << i << "] Failed to acquire connection" << std::endl;
                    failed++;
                    return;
                }

                RunParallelSmokeTest(conn, i, test_url, passed, failed, verbose);

                pool.ReleaseConnection(conn);
            });
        }

        // Wait for all threads to complete
        for (auto& t : threads) {
            t.join();
        }

        auto end = std::chrono::steady_clock::now();
        double duration_sec = std::chrono::duration<double>(end - start).count();

        std::cout << "\n========================================\n";
        std::cout << "PARALLEL TEST SUMMARY\n";
        std::cout << "========================================\n";
        std::cout << "Threads:      " << concurrency << "\n";
        std::cout << "Passed:       " << passed.load() << "\n";
        std::cout << "Failed:       " << failed.load() << "\n";
        std::cout << "Duration:     " << std::fixed << std::setprecision(2) << duration_sec << "s\n";
        std::cout << "Throughput:   " << std::setprecision(1) << (concurrency / duration_sec) << " tests/s\n";
        std::cout << "========================================\n";

        all_passed = (failed.load() == 0);
    }
    else if (mode == "benchmark") {
        std::cout << "\n[INFO] Running benchmark (" << iterations << " iterations)...\n" << std::endl;

        // Create a context
        auto ctx_result = client.Send("createContext", {});
        if (ResponseValidator::ValidateContextId(ctx_result)) {
            std::string ctx = ResponseValidator::GetStringResult(ctx_result);
            runner.SetActiveContext(ctx);

            // Navigate once
            client.Send("navigate", {{"context_id", ctx}, {"url", test_url}});
            client.Send("waitForNavigation", {{"context_id", ctx}, {"timeout", 15000}});

            // Benchmark key operations
            for (int i = 0; i < iterations; i++) {
                runner.Test("screenshot", {{"context_id", ctx}}, "benchmark");
                runner.Test("extractText", {{"context_id", ctx}}, "benchmark");
                runner.Test("getHTML", {{"context_id", ctx}}, "benchmark");
                runner.Test("getPageInfo", {{"context_id", ctx}}, "benchmark");
            }

            client.Send("releaseContext", {{"context_id", ctx}});
        }

        all_passed = runner.PrintSummary();
    }
    else if (mode == "stress") {
        std::cout << "\n[INFO] Running stress test (" << contexts << " contexts, "
                  << duration << "s)...\n" << std::endl;

        std::vector<std::string> ctx_ids;

        // Create multiple contexts
        for (int i = 0; i < contexts; i++) {
            auto result = client.Send("createContext", {});
            if (ResponseValidator::ValidateContextId(result)) {
                ctx_ids.push_back(ResponseValidator::GetStringResult(result));
            }
        }

        std::cout << "[INFO] Created " << ctx_ids.size() << " contexts" << std::endl;

        // Run operations for duration
        auto start = std::chrono::steady_clock::now();
        int command_count = 0;

        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= duration) break;

            for (const auto& ctx : ctx_ids) {
                client.Send("getPageInfo", {{"context_id", ctx}});
                command_count++;
            }
        }

        std::cout << "[INFO] Executed " << command_count << " commands in " << duration << "s" << std::endl;
        std::cout << "[INFO] Throughput: " << (command_count / duration) << " cmd/s" << std::endl;

        // Cleanup
        for (const auto& ctx : ctx_ids) {
            client.Send("releaseContext", {{"context_id", ctx}});
        }

        all_passed = true;
    }
    else if (mode == "leak-check") {
        std::cout << "\n[INFO] Running memory leak check (" << duration << "s)...\n" << std::endl;

        int64_t initial_memory = monitor.GetCurrentMemoryBytes();
        std::cout << "[INFO] Initial memory: " << (initial_memory / (1024 * 1024)) << " MB" << std::endl;

        auto start = std::chrono::steady_clock::now();

        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= duration) break;

            // Create and destroy contexts
            auto result = client.Send("createContext", {});
            if (ResponseValidator::ValidateContextId(result)) {
                std::string ctx = ResponseValidator::GetStringResult(result);
                client.Send("navigate", {{"context_id", ctx}, {"url", test_url}});
                client.Send("waitForNavigation", {{"context_id", ctx}, {"timeout", 5000}});
                client.Send("screenshot", {{"context_id", ctx}});
                client.Send("releaseContext", {{"context_id", ctx}});
            }

            if (elapsed % 10 == 0) {
                int64_t current = monitor.GetCurrentMemoryBytes();
                std::cout << "[INFO] Memory at " << elapsed << "s: "
                          << (current / (1024 * 1024)) << " MB" << std::endl;
            }
        }

        int64_t final_memory = monitor.GetCurrentMemoryBytes();
        int64_t memory_growth = final_memory - initial_memory;

        std::cout << "\n[INFO] Final memory: " << (final_memory / (1024 * 1024)) << " MB" << std::endl;
        std::cout << "[INFO] Memory growth: " << (memory_growth / (1024 * 1024)) << " MB" << std::endl;

        // Consider it a failure if memory grew by more than 100MB
        all_passed = (memory_growth < 100 * 1024 * 1024);
        if (!all_passed) {
            std::cerr << "[WARN] Potential memory leak detected!" << std::endl;
        }
    }

    // Stop resource monitor
    monitor.Stop();

    // Generate reports
    if (!json_report_path.empty() || !html_report_path.empty()) {
        std::cout << "\n[INFO] Generating reports..." << std::endl;

        ReportMetadata metadata;
        metadata.test_run_id = GenerateUUID();
        metadata.timestamp = GetTimestamp();
        metadata.test_mode = mode;
        metadata.browser_version = "1.0.0";
        metadata.browser_path = browser_path;
        metadata.platform = GetPlatform();
        metadata.platform_version = GetPlatformVersion();
        metadata.cpu_model = GetCPUModel();
        metadata.total_memory_gb = GetTotalMemoryGB();

        ReportGenerator report_gen;
        report_gen.SetMetadata(metadata);
        report_gen.SetResults(runner.GetResults());
        report_gen.SetResourceTimeline(monitor.GetAllSamples());
        report_gen.SetBenchmarkStats(runner.CalculateStats());
        report_gen.SetCategoryStats(runner.GetCategoryStats());

        if (!json_report_path.empty()) {
            if (report_gen.SaveJSON(json_report_path)) {
                std::cout << "[INFO] JSON report saved to: " << json_report_path << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to save JSON report" << std::endl;
            }
        }

        if (!html_report_path.empty()) {
            json report_data = report_gen.GenerateJSON();
            if (HTMLReportGenerator::SaveHTML(report_data, html_report_path)) {
                std::cout << "[INFO] HTML report saved to: " << html_report_path << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to save HTML report" << std::endl;
            }
        }
    }

    // Stop browser
    std::cout << "\n[INFO] Stopping browser..." << std::endl;
    client.Stop();

    std::cout << "\n========================================\n";
    std::cout << (all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    std::cout << "========================================\n";

    return all_passed ? 0 : 1;
}
