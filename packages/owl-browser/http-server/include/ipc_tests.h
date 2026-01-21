/**
 * Owl Browser HTTP Server - IPC Tests Module
 *
 * Provides functionality to run the ipc_test_client binary and manage test reports.
 * This module is conditionally enabled via OWL_IPC_TESTS_ENABLED environment variable.
 */

#ifndef OWL_HTTP_IPC_TESTS_H
#define OWL_HTTP_IPC_TESTS_H

#include <stdbool.h>
#include <stddef.h>

// Maximum path length for reports
#define IPC_TEST_MAX_PATH 4096

// Test modes matching ipc_test_client CLI options
typedef enum {
    IPC_TEST_MODE_SMOKE,      // Quick critical path validation (5 tests)
    IPC_TEST_MODE_FULL,       // Comprehensive validation (138+ tests)
    IPC_TEST_MODE_BENCHMARK,  // Performance testing with iterations
    IPC_TEST_MODE_STRESS,     // Load testing with multiple contexts
    IPC_TEST_MODE_LEAK_CHECK, // Memory leak detection
    IPC_TEST_MODE_PARALLEL    // Concurrent browser context testing
} IpcTestMode;

// Test run status
typedef enum {
    IPC_TEST_STATUS_IDLE,
    IPC_TEST_STATUS_RUNNING,
    IPC_TEST_STATUS_COMPLETED,
    IPC_TEST_STATUS_FAILED,
    IPC_TEST_STATUS_ABORTED
} IpcTestStatus;

// Test run configuration
typedef struct {
    IpcTestMode mode;
    bool verbose;
    int iterations;       // For benchmark mode
    int contexts;         // For stress mode
    int duration_seconds; // For stress/leak-check modes
    int concurrency;      // For parallel mode
} IpcTestConfig;

// Test run result
typedef struct {
    char run_id[64];           // Unique run identifier
    IpcTestStatus status;
    int exit_code;
    char json_report_path[IPC_TEST_MAX_PATH];
    char html_report_path[IPC_TEST_MAX_PATH];
    char error_message[1024];
    // Summary statistics (from JSON report)
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    double duration_seconds;
    double commands_per_second;
} IpcTestResult;

// Report info for listing
typedef struct {
    char run_id[64];
    char timestamp[64];
    char mode[32];
    int total_tests;
    int passed_tests;
    int failed_tests;
    double duration_seconds;
    char json_report_path[IPC_TEST_MAX_PATH];
    char html_report_path[IPC_TEST_MAX_PATH];
} IpcTestReportInfo;

/**
 * Initialize the IPC tests module.
 *
 * @param test_client_path Path to ipc_test_client binary
 * @param browser_path Path to owl_browser binary
 * @param reports_dir Directory to store test reports
 * @return 0 on success, -1 on error
 */
int ipc_tests_init(const char* test_client_path, const char* browser_path,
                   const char* reports_dir);

/**
 * Shutdown the IPC tests module.
 */
void ipc_tests_shutdown(void);

/**
 * Check if IPC tests module is enabled/available.
 */
bool ipc_tests_is_enabled(void);

/**
 * Start a new test run asynchronously.
 *
 * @param config Test configuration
 * @param result Output result structure (run_id will be set)
 * @return 0 on success, -1 on error (e.g., another test already running)
 */
int ipc_tests_start(const IpcTestConfig* config, IpcTestResult* result);

/**
 * Get the status of a test run.
 *
 * @param run_id The run ID to query
 * @param result Output result structure
 * @return 0 on success, -1 if run not found
 */
int ipc_tests_get_status(const char* run_id, IpcTestResult* result);

/**
 * Get the status of the current/last test run.
 *
 * @param result Output result structure
 * @return 0 on success, -1 if no test has been run
 */
int ipc_tests_get_current_status(IpcTestResult* result);

/**
 * Abort a running test.
 *
 * @param run_id The run ID to abort
 * @return 0 on success, -1 on error
 */
int ipc_tests_abort(const char* run_id);

/**
 * List all available test reports.
 *
 * @param reports Output array of report info
 * @param max_reports Maximum number of reports to return
 * @return Number of reports found, -1 on error
 */
int ipc_tests_list_reports(IpcTestReportInfo* reports, int max_reports);

/**
 * Get a specific report's JSON content.
 *
 * @param run_id The run ID
 * @param content Output buffer for JSON content (caller must free)
 * @return 0 on success, -1 on error
 */
int ipc_tests_get_json_report(const char* run_id, char** content);

/**
 * Get a specific report's HTML content.
 *
 * @param run_id The run ID
 * @param content Output buffer for HTML content (caller must free)
 * @return 0 on success, -1 on error
 */
int ipc_tests_get_html_report(const char* run_id, char** content);

/**
 * Delete a test report.
 *
 * @param run_id The run ID to delete
 * @return 0 on success, -1 on error
 */
int ipc_tests_delete_report(const char* run_id);

/**
 * Delete all test reports.
 *
 * @return Number of files deleted, -1 on error
 */
int ipc_tests_clean_all_reports(void);

/**
 * Convert IpcTestMode to string.
 */
const char* ipc_test_mode_to_string(IpcTestMode mode);

/**
 * Parse IpcTestMode from string.
 */
IpcTestMode ipc_test_mode_from_string(const char* str);

/**
 * Convert IpcTestStatus to string.
 */
const char* ipc_test_status_to_string(IpcTestStatus status);

#endif // OWL_HTTP_IPC_TESTS_H
