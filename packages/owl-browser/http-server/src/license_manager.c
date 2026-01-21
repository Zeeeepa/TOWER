/**
 * Owl Browser HTTP Server - License Manager Implementation
 *
 * Runs browser binary with --license commands to manage licenses
 * when browser IPC is not available.
 */

#include "license_manager.h"
#include "json.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

static char g_browser_path[4096] = {0};

// Base64 decoding table
static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

// Decode base64 string, returns decoded data and sets out_len
static unsigned char* base64_decode(const char* input, size_t* out_len) {
    size_t input_len = strlen(input);
    if (input_len == 0) {
        *out_len = 0;
        return NULL;
    }

    // Calculate output size (3 bytes per 4 input chars, minus padding)
    size_t output_len = (input_len / 4) * 3;
    if (input[input_len - 1] == '=') output_len--;
    if (input[input_len - 2] == '=') output_len--;

    unsigned char* output = malloc(output_len + 1);
    if (!output) return NULL;

    size_t i = 0, j = 0;
    while (i < input_len) {
        unsigned char a = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;
        unsigned char b = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;
        unsigned char c = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;
        unsigned char d = (i < input_len) ? base64_decode_table[(unsigned char)input[i++]] : 0;

        if (a == 64 || b == 64) break;  // Invalid character

        output[j++] = (a << 2) | (b >> 4);
        if (c != 64 && j < output_len) output[j++] = (b << 4) | (c >> 2);
        if (d != 64 && j < output_len) output[j++] = (c << 6) | d;
    }

    *out_len = j;
    output[j] = '\0';
    return output;
}

// Run a command and capture output
static int run_command(char* const argv[], char* output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_ERROR("LicenseManager", "pipe() failed: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOG_ERROR("LicenseManager", "fork() failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execv(argv[0], argv);
        _exit(127);  // exec failed
    }

    // Parent process
    close(pipefd[1]);  // Close write end

    // Read output
    size_t total = 0;
    ssize_t n;
    while (total < output_size - 1 &&
           (n = read(pipefd[0], output + total, output_size - total - 1)) > 0) {
        total += n;
    }
    output[total] = '\0';
    close(pipefd[0]);

    // Wait for child
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

void license_manager_init(const char* browser_path) {
    if (browser_path) {
        strncpy(g_browser_path, browser_path, sizeof(g_browser_path) - 1);
    }
    LOG_INFO("LicenseManager", "Initialized with browser: %s", g_browser_path);
}

void license_manager_shutdown(void) {
    g_browser_path[0] = '\0';
}

bool license_manager_get_status(LicenseInfo* info) {
    if (!info) return false;
    memset(info, 0, sizeof(LicenseInfo));

    if (g_browser_path[0] == '\0') {
        strncpy(info->status, "error", sizeof(info->status) - 1);
        strncpy(info->message, "Browser path not configured", sizeof(info->message) - 1);
        return false;
    }

    char* argv[] = {g_browser_path, "--license", "status", "--json", NULL};
    char output[8192];

    int ret = run_command(argv, output, sizeof(output));

    // Try to parse JSON output
    JsonValue* root = json_parse(output);
    if (root && root->type == JSON_OBJECT) {
        const char* status = json_object_get_string(root, "status");
        if (status) {
            strncpy(info->status, status, sizeof(info->status) - 1);
            info->valid = (strcmp(status, "valid") == 0);
        }

        const char* message = json_object_get_string(root, "message");
        if (message) strncpy(info->message, message, sizeof(info->message) - 1);

        const char* fingerprint = json_object_get_string(root, "fingerprint");
        if (fingerprint) strncpy(info->fingerprint, fingerprint, sizeof(info->fingerprint) - 1);

        const char* licensee = json_object_get_string(root, "licensee");
        if (licensee) strncpy(info->licensee, licensee, sizeof(info->licensee) - 1);

        const char* license_type = json_object_get_string(root, "license_type");
        if (license_type) strncpy(info->license_type, license_type, sizeof(info->license_type) - 1);

        info->seat_current = (int)json_object_get_int(root, "seat_current", 0);
        info->seat_total = (int)json_object_get_int(root, "seat_total", 0);

        const char* expiry = json_object_get_string(root, "expiry");
        if (expiry) strncpy(info->expiry, expiry, sizeof(info->expiry) - 1);

        json_free(root);
        return true;
    }

    // Fallback: parse text output
    if (strstr(output, "not_found") || strstr(output, "No license")) {
        strncpy(info->status, "not_found", sizeof(info->status) - 1);
        strncpy(info->message, "No license file found", sizeof(info->message) - 1);
    } else if (strstr(output, "expired")) {
        strncpy(info->status, "expired", sizeof(info->status) - 1);
        strncpy(info->message, "License has expired", sizeof(info->message) - 1);
    } else if (strstr(output, "invalid")) {
        strncpy(info->status, "invalid", sizeof(info->status) - 1);
        strncpy(info->message, "License is invalid", sizeof(info->message) - 1);
    } else if (ret == 0) {
        strncpy(info->status, "valid", sizeof(info->status) - 1);
        info->valid = true;
    } else {
        strncpy(info->status, "error", sizeof(info->status) - 1);
        strncpy(info->message, output, sizeof(info->message) - 1);
    }

    // Try to extract fingerprint from output
    char* fp_start = strstr(output, "Fingerprint:");
    if (fp_start) {
        fp_start += 12;
        while (*fp_start == ' ') fp_start++;
        char* fp_end = strchr(fp_start, '\n');
        if (fp_end) {
            size_t len = fp_end - fp_start;
            if (len < sizeof(info->fingerprint)) {
                strncpy(info->fingerprint, fp_start, len);
                info->fingerprint[len] = '\0';
            }
        }
    }

    json_free(root);
    return ret == 0;
}

bool license_manager_get_fingerprint(char* fingerprint, size_t size) {
    if (!fingerprint || size == 0) return false;
    fingerprint[0] = '\0';

    if (g_browser_path[0] == '\0') {
        return false;
    }

    char* argv[] = {g_browser_path, "--license", "fingerprint", NULL};
    char output[1024];

    int ret = run_command(argv, output, sizeof(output));

    // Extract fingerprint (should be on its own line or after "Fingerprint:")
    char* fp = strstr(output, "Fingerprint:");
    if (fp) {
        fp += 12;
        while (*fp == ' ') fp++;
    } else {
        fp = output;
    }

    // Remove trailing newlines
    char* newline = strchr(fp, '\n');
    if (newline) *newline = '\0';

    strncpy(fingerprint, fp, size - 1);
    fingerprint[size - 1] = '\0';

    return ret == 0 && strlen(fingerprint) > 0;
}

bool license_manager_add_license(const char* license_path, LicenseOpResult* result) {
    if (!result) return false;
    memset(result, 0, sizeof(LicenseOpResult));

    if (!license_path || license_path[0] == '\0') {
        strncpy(result->error, "License path is required", sizeof(result->error) - 1);
        return false;
    }

    if (g_browser_path[0] == '\0') {
        strncpy(result->error, "Browser path not configured", sizeof(result->error) - 1);
        return false;
    }

    char* argv[] = {g_browser_path, "--license", "add", (char*)license_path, NULL};
    char output[8192];

    int ret = run_command(argv, output, sizeof(output));

    if (ret == 0) {
        result->success = true;
        strncpy(result->message, "License added successfully", sizeof(result->message) - 1);
    } else {
        result->success = false;
        // Extract error message from output
        if (strlen(output) > 0) {
            strncpy(result->error, output, sizeof(result->error) - 1);
        } else {
            strncpy(result->error, "Failed to add license", sizeof(result->error) - 1);
        }
    }

    return result->success;
}

bool license_manager_add_license_content(const char* license_content, LicenseOpResult* result) {
    if (!result) return false;
    memset(result, 0, sizeof(LicenseOpResult));

    if (!license_content || license_content[0] == '\0') {
        strncpy(result->error, "License content is required", sizeof(result->error) - 1);
        return false;
    }

    if (g_browser_path[0] == '\0') {
        strncpy(result->error, "Browser path not configured", sizeof(result->error) - 1);
        return false;
    }

    // Decode base64 content
    size_t decoded_len = 0;
    unsigned char* decoded = base64_decode(license_content, &decoded_len);
    if (!decoded || decoded_len == 0) {
        strncpy(result->error, "Failed to decode base64 license content", sizeof(result->error) - 1);
        if (decoded) free(decoded);
        return false;
    }

    // Create temp file
    char temp_path[256] = "/tmp/owl_license_XXXXXX.olic";
    int fd = mkstemps(temp_path, 5);  // 5 for ".olic"
    if (fd == -1) {
        strncpy(result->error, "Failed to create temporary file", sizeof(result->error) - 1);
        free(decoded);
        return false;
    }

    // Write decoded content
    ssize_t written = write(fd, decoded, decoded_len);
    close(fd);
    free(decoded);

    if (written != (ssize_t)decoded_len) {
        unlink(temp_path);
        strncpy(result->error, "Failed to write license to temporary file", sizeof(result->error) - 1);
        return false;
    }

    // Add license using the temp file
    bool success = license_manager_add_license(temp_path, result);

    // Clean up temp file
    unlink(temp_path);

    return success;
}

bool license_manager_remove_license(LicenseOpResult* result) {
    if (!result) return false;
    memset(result, 0, sizeof(LicenseOpResult));

    if (g_browser_path[0] == '\0') {
        strncpy(result->error, "Browser path not configured", sizeof(result->error) - 1);
        return false;
    }

    char* argv[] = {g_browser_path, "--license", "remove", NULL};
    char output[8192];

    int ret = run_command(argv, output, sizeof(output));

    if (ret == 0) {
        result->success = true;
        strncpy(result->message, "License removed successfully", sizeof(result->message) - 1);
    } else {
        result->success = false;
        if (strlen(output) > 0) {
            strncpy(result->error, output, sizeof(result->error) - 1);
        } else {
            strncpy(result->error, "Failed to remove license", sizeof(result->error) - 1);
        }
    }

    return result->success;
}

char* license_manager_status_to_json(const LicenseInfo* info) {
    if (!info) return strdup("{\"success\":false,\"error\":\"No info\"}");

    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);

    json_builder_key(&builder, "success");
    json_builder_bool(&builder, true);
    json_builder_comma(&builder);

    json_builder_key(&builder, "status");
    json_builder_string(&builder, info->valid ? "ok" : "error");
    json_builder_comma(&builder);

    json_builder_key(&builder, "data");
    json_builder_object_start(&builder);

    json_builder_key(&builder, "license_status");
    json_builder_string(&builder, info->status);

    if (info->message[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "message");
        json_builder_string(&builder, info->message);
    }

    if (info->fingerprint[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "fingerprint");
        json_builder_string(&builder, info->fingerprint);
    }

    if (info->licensee[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "licensee");
        json_builder_string(&builder, info->licensee);
    }

    if (info->license_type[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "license_type");
        json_builder_string(&builder, info->license_type);
    }

    if (info->seat_total > 0) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "seat_current");
        json_builder_int(&builder, info->seat_current);
        json_builder_comma(&builder);
        json_builder_key(&builder, "seat_total");
        json_builder_int(&builder, info->seat_total);
    }

    if (info->expiry[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "expiry");
        json_builder_string(&builder, info->expiry);
    }

    json_builder_object_end(&builder);  // data

    json_builder_object_end(&builder);  // root

    return json_builder_finish(&builder);
}

char* license_manager_result_to_json(const LicenseOpResult* result) {
    if (!result) return strdup("{\"success\":false,\"error\":\"No result\"}");

    JsonBuilder builder;
    json_builder_init(&builder);

    json_builder_object_start(&builder);

    json_builder_key(&builder, "success");
    json_builder_bool(&builder, result->success);
    json_builder_comma(&builder);

    json_builder_key(&builder, "status");
    json_builder_string(&builder, result->success ? "ok" : "error");

    if (result->success && result->message[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "message");
        json_builder_string(&builder, result->message);
    }

    if (!result->success && result->error[0]) {
        json_builder_comma(&builder);
        json_builder_key(&builder, "error");
        json_builder_string(&builder, result->error);
    }

    json_builder_object_end(&builder);

    return json_builder_finish(&builder);
}
