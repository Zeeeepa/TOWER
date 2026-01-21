#pragma once

#include "test_runner.h"
#include "ipc_client.h"
#include <string>

// Run all 138 method tests
// Returns true if all tests pass
bool RunAllMethodTests(TestRunner& runner, IPCClient& client, const std::string& test_url = "owl://user_form.html/");

// Test categories
void RunContextManagementTests(TestRunner& runner, IPCClient& client);
void RunNavigationTests(TestRunner& runner, IPCClient& client, const std::string& test_url);
void RunElementInteractionTests(TestRunner& runner, IPCClient& client, const std::string& test_url);
void RunMouseDragTests(TestRunner& runner, IPCClient& client);
void RunElementStateTests(TestRunner& runner, IPCClient& client);
void RunJavaScriptTests(TestRunner& runner, IPCClient& client);
void RunContentExtractionTests(TestRunner& runner, IPCClient& client);
void RunScreenshotVisualTests(TestRunner& runner, IPCClient& client);
void RunScrollingTests(TestRunner& runner, IPCClient& client);
void RunWaitTimingTests(TestRunner& runner, IPCClient& client);
void RunPageStateTests(TestRunner& runner, IPCClient& client);
void RunViewportTests(TestRunner& runner, IPCClient& client);
void RunVideoRecordingTests(TestRunner& runner, IPCClient& client);
void RunLiveStreamingTests(TestRunner& runner, IPCClient& client);
void RunCaptchaTests(TestRunner& runner, IPCClient& client);
void RunCookieTests(TestRunner& runner, IPCClient& client);
void RunProxyTests(TestRunner& runner, IPCClient& client);
void RunProfileTests(TestRunner& runner, IPCClient& client);
void RunFileTests(TestRunner& runner, IPCClient& client);
void RunFrameTests(TestRunner& runner, IPCClient& client);
void RunNetworkTests(TestRunner& runner, IPCClient& client);
void RunDownloadTests(TestRunner& runner, IPCClient& client);
void RunDialogTests(TestRunner& runner, IPCClient& client);
void RunTabTests(TestRunner& runner, IPCClient& client);
void RunAILLMTests(TestRunner& runner, IPCClient& client);
void RunElementFindingTests(TestRunner& runner, IPCClient& client);
void RunDemographicsTests(TestRunner& runner, IPCClient& client);
void RunLicenseSystemTests(TestRunner& runner, IPCClient& client);
void RunConsoleLoggingTests(TestRunner& runner, IPCClient& client);

// Error handling tests
void RunErrorHandlingTests(TestRunner& runner, IPCClient& client);
