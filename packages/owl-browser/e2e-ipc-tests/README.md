# Owl Browser IPC Test Client

A standalone C++ test client for testing the Owl Browser IPC protocol. Tests all 138 IPC methods with comprehensive benchmarking, parallel execution, and reporting.

## Features

- **Dual Connection Modes**: Unix Domain Socket (default) or stdin/stdout pipes
- **Parallel Testing**: Run concurrent browser contexts with connection pooling
- **Full Test Coverage**: 138 IPC method tests with validation
- **Performance Metrics**: Latency stats, throughput, P95/P99 percentiles
- **Resource Monitoring**: Memory and CPU usage tracking
- **Rich Reporting**: JSON and interactive HTML reports

## Building

### Prerequisites

- CMake 3.16+
- C++17 compiler (clang++ or g++)
- macOS or Linux
- pthreads (for parallel testing)

### Build Steps

```bash
cd e2e-ipc-tests
mkdir build && cd build
cmake ..
make -j4
```

The binary `ipc_test_client` will be created in the build directory.

## Usage

```bash
./ipc_test_client [OPTIONS]
```

### Options

| Option | Description |
|--------|-------------|
| `--browser-path PATH` | Path to owl_browser binary (auto-detected if not specified) |
| `--test-url URL` | URL for testing (default: owl://user_form.html/) |
| `--mode MODE` | Test mode: `smoke`, `full`, `benchmark`, `stress`, `leak-check`, `parallel` |
| `--connection-mode M` | Connection mode: `auto`, `socket`, `pipe` (default: auto) |
| `--concurrency N` | Number of parallel threads for parallel mode (default: 1) |
| `--verbose` | Enable verbose output |
| `--json-report FILE` | Output JSON report to file |
| `--html-report FILE` | Output HTML report to file |
| `--iterations N` | Number of iterations for benchmark mode |
| `--contexts N` | Number of contexts for stress mode |
| `--duration N` | Duration in seconds for stress/leak-check modes |
| `--help` | Show help message |

### Connection Modes

| Mode | Description |
|------|-------------|
| `auto` | Try socket first, fallback to pipe (default) |
| `socket` | Unix Domain Socket only - required for parallel testing |
| `pipe` | stdin/stdout pipes only (legacy mode) |

The browser creates a socket at `/tmp/owl_browser_{instance_id}.sock` which supports multiple concurrent connections. Socket mode provides better performance and enables true parallel testing.

## Test Modes

### Smoke Test
Quick critical path validation (5 tests):
```bash
./ipc_test_client --mode smoke --verbose
```

### Full Test
All 138 method tests with validation:
```bash
./ipc_test_client --mode full --json-report report.json --html-report report.html
```

### Parallel Test
Run concurrent browser contexts using socket connection pool:
```bash
# Run with 4 parallel threads
./ipc_test_client --mode parallel --concurrency 4

# Run with 8 parallel threads
./ipc_test_client --mode parallel --concurrency 8 --verbose
```

Each thread creates its own browser context and runs the smoke test suite concurrently. Socket mode is automatically enabled for parallel testing.

### Benchmark
Performance testing with multiple iterations:
```bash
./ipc_test_client --mode benchmark --iterations 100
```

### Stress Test
Load testing with multiple concurrent contexts:
```bash
./ipc_test_client --mode stress --contexts 20 --duration 120
```

### Memory Leak Check
Monitor memory growth over time:
```bash
./ipc_test_client --mode leak-check --duration 300
```

## Example Runs

```bash
# Quick smoke test (auto-detects socket mode)
./build/ipc_test_client --browser-path ../build/Release/owl_browser.app/Contents/MacOS/owl_browser --mode smoke

# Full test with socket mode explicitly
./build/ipc_test_client --browser-path ../build/Release/owl_browser.app/Contents/MacOS/owl_browser --mode full --connection-mode socket

# Full test with legacy pipe mode
./build/ipc_test_client --browser-path ../build/Release/owl_browser.app/Contents/MacOS/owl_browser --mode full --connection-mode pipe

# Full test with reports
./build/ipc_test_client --browser-path ../build/Release/owl_browser.app/Contents/MacOS/owl_browser --mode full --verbose --json-report /tmp/report.json --html-report /tmp/report.html

# Parallel test with 4 concurrent threads
./build/ipc_test_client --browser-path ../build/Release/owl_browser.app/Contents/MacOS/owl_browser --mode parallel --concurrency 4

# Benchmark screenshot performance
./build/ipc_test_client --mode benchmark --iterations 50 --verbose
```

## Output

### Console Output
```
========================================
OWL BROWSER IPC TEST CLIENT
========================================
Browser:    ./owl_browser
Mode:       full
Connection: socket
URL:        owl://user_form.html/
========================================

[INFO] Starting browser...
[INFO] Browser started (PID: 12345)
[INFO] Connection mode: socket (/tmp/owl_browser_default.sock)
Running all 135 IPC method tests...

[PASS] createContext (124.5ms)
[PASS] navigate (15.2ms)
[PASS] waitForNavigation (258.4ms)
...
[PASS] releaseContext (6.1ms)

========================================
TEST SUMMARY
========================================
Total:  138
Passed: 138
Failed: 0
========================================

LATENCY STATS:
  Min:    0.15ms
  Max:    3550.97ms
  Avg:    118.51ms
  Median: 8.39ms
  P95:    675.66ms
  P99:    3549.64ms
  StdDev: 446.68ms

THROUGHPUT:
  Commands/sec: 8.4
  Duration:     16.35s
```

### Parallel Test Output
```
========================================
OWL BROWSER IPC TEST CLIENT
========================================
Browser:    ./owl_browser
Mode:       parallel
Connection: auto
Concurrency: 4 threads
URL:        owl://user_form.html/
========================================

[INFO] Starting browser...
[INFO] Browser started (PID: 56060)
[INFO] Connection mode: socket (/tmp/owl_browser_default.sock)

[INFO] Running parallel test with 4 threads...
[Pool] Initialized 4/4 connections to /tmp/owl_browser_default.sock

========================================
PARALLEL TEST SUMMARY
========================================
Threads:      4
Passed:       4
Failed:       0
Duration:     5.02s
Throughput:   0.8 tests/s
========================================
```

### JSON Report
Contains:
- Test metadata (timestamp, platform, browser version)
- Summary statistics (total/passed/failed)
- Latency statistics (min/max/avg/median/P95/P99)
- Per-category breakdown
- Individual test results with timing
- Resource usage timeline

### HTML Report
Interactive dashboard with:
- Summary cards
- Pass/fail pie chart
- Latency distribution histogram
- Category breakdown bar chart
- Detailed results table
- Resource usage graphs

## Test Categories

| Category | Methods |
|----------|---------|
| Context Management | createContext, listContexts, releaseContext |
| Navigation | navigate, waitForNavigation, reload, goBack, goForward, canGoBack, canGoForward |
| Element Interaction | click, type, pick, pressKey, submitForm, hover, doubleClick, rightClick, clearInput, selectAll, focus, blur, keyboardCombo |
| Mouse & Drag | dragDrop, html5DragDrop, mouseMove |
| Element State | isVisible, isEnabled, isChecked, getAttribute, getBoundingBox, getElementAtPosition, getInteractiveElements |
| JavaScript | evaluate (with return_value option) |
| Clipboard | clipboardRead, clipboardWrite, clipboardClear |
| Content Extraction | extractText, getHTML, getMarkdown, extractJSON, detectWebsiteType, listTemplates |
| Screenshot & Visual | screenshot, highlight, showGridOverlay |
| Scrolling | scrollBy, scrollTo, scrollToElement, scrollToTop, scrollToBottom |
| Wait & Timing | waitForSelector, waitForTimeout, waitForNetworkIdle, waitForFunction, waitForURL |
| Page State | getCurrentURL, getPageTitle, getPageInfo |
| Viewport | setViewport, getViewport |
| Video Recording | startVideoRecording, pauseVideoRecording, resumeVideoRecording, getVideoRecordingStats, stopVideoRecording |
| Live Streaming | startLiveStream, listLiveStreams, getLiveStreamStats, getLiveFrame, stopLiveStream |
| CAPTCHA | detectCaptcha, classifyCaptcha, solveTextCaptcha, solveImageCaptcha, solveCaptcha |
| Cookies | setCookie, getCookies, deleteCookies |
| Proxy | setProxy, getProxyStatus, connectProxy, disconnectProxy |
| Profile | createProfile, getProfile, getContextInfo, updateProfileCookies, saveProfile |
| Files | uploadFile |
| Frames | listFrames, switchToFrame, switchToMainFrame |
| Network | enableNetworkInterception, enableNetworkLogging, addNetworkRule, getNetworkLog, clearNetworkLog, removeNetworkRule |
| Downloads | setDownloadPath, getDownloads, getActiveDownloads, waitForDownload, cancelDownload |
| Dialogs | setDialogAction, getPendingDialog, getDialogs, waitForDialog, handleDialog |
| Tabs | setPopupPolicy, getTabs, getActiveTab, getTabCount, newTab, switchTab, getBlockedPopups, closeTab |
| AI/LLM | getLLMStatus, summarizePage, queryPage, executeNLA, aiClick, aiType, aiExtract, aiQuery, aiAnalyze |
| Element Finding | findElement, getBlockerStats |
| Demographics | getDemographics, getLocation, getDateTime, getWeather, getHomepage |
| License | getLicenseStatus, getLicenseInfo, getHardwareFingerprint, addLicense, removeLicense |

## Architecture

```
e2e-ipc-tests/
├── CMakeLists.txt          # Build configuration (with pthreads)
├── README.md               # This file
├── json.hpp                # nlohmann/json library
├── benchmark_stats.h       # Metrics data structures
├── ipc_client.h/cc         # IPC client with socket & pipe support
├── response_validator.h/cc # Response type validation
├── resource_monitor.h/cc   # Memory/CPU monitoring
├── test_runner.h/cc        # Test execution framework
├── report_generator.h/cc   # JSON report generation
├── html_report_generator.h/cc # HTML report generation
├── method_tests.h/cc       # All 138 method tests
└── main.cc                 # CLI entry point
```

### IPC Client Classes

| Class | Description |
|-------|-------------|
| `IPCClient` | Main client - spawns browser, supports socket and pipe modes |
| `SocketClient` | Standalone socket-only client for connection pooling |
| `IPCConnectionPool` | Thread-safe connection pool for parallel testing |
| `ConnectionMode` | Enum: `AUTO`, `SOCKET`, `PIPE` |

### Socket Protocol

The browser uses newline-delimited JSON over Unix Domain Sockets:

```
Client → Browser: {"id": 1, "method": "createContext"}\n
Browser → Client: {"id": 1, "result": {"context_id": "ctx_123"}}\n
```

Socket path: `/tmp/owl_browser_{instance_id}.sock`

## Response Validation

The client validates 5 response types:
- **String**: `{"id": N, "result": "string_value"}`
- **Boolean**: `{"id": N, "result": true/false}`
- **JSON**: `{"id": N, "result": {...}}`
- **ActionResult**: `{"id": N, "result": {"status": "...", "message": "..."}}`
- **Error**: `{"id": N, "error": "..."}`

And 28 ActionStatus codes including: `success`, `element_not_found`, `navigation_failed`, `timeout`, etc.

## Troubleshooting

### Browser not starting
- Ensure the browser path is correct
- Check if a license is required: `owl_browser --license status`
- Kill any zombie processes: `pkill -9 owl_browser`

### Socket connection failed
- Check if socket file exists: `ls -la /tmp/owl_browser_*.sock`
- Remove stale socket files: `rm /tmp/owl_browser_*.sock`
- Ensure browser supports multi-IPC (look for `MULTI_IPC_READY` in startup)
- Use `--connection-mode pipe` as fallback

### Timeouts
- Some operations (navigation, screenshot) take longer
- Use `--verbose` to see which operations are slow
- Increase timeout in code if needed (default: 30s per command)
- Large responses (like getHomepage) may need more time

### Parallel test failures
- Ensure socket mode is working: `--connection-mode socket`
- Reduce concurrency if system resources are limited
- Check for sufficient file descriptors: `ulimit -n`

### Socket conflicts
- The browser creates sockets at `/tmp/owl_browser_{instance_id}.sock`
- Ensure no other browser instances are running with the same instance ID
- Kill zombie processes: `pkill -9 owl_browser`
