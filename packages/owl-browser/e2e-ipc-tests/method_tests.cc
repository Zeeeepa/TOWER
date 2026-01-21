#include "method_tests.h"
#include "response_validator.h"
#include <iostream>
#include <thread>
#include <chrono>

// Helper to wait for page load - ensures page is actually ready
static void WaitForPageLoad(IPCClient& client, const std::string& ctx, int timeout_ms = 15000) {
    // First try waitForNavigation
    auto nav_result = client.Send("waitForNavigation", {{"context_id", ctx}, {"timeout", timeout_ms}});

    // Then use waitForSelector to ensure DOM is ready
    auto selector_result = client.Send("waitForSelector",
        {{"context_id", ctx}, {"selector", "body"}, {"timeout", timeout_ms}});

    // Small delay to ensure all resources are loaded
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ============================================================================
// CONTEXT MANAGEMENT (3 methods)
// ============================================================================
void RunContextManagementTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "context_management";

    // 1. createContext - basic
    auto r1 = runner.TestWithValidator("createContext",
        [](const json& resp) {
            return ResponseValidator::ValidateContextId(resp);
        }, {}, CAT);

    std::string ctx1 = ResponseValidator::GetStringResult(r1.response);
    runner.SetActiveContext(ctx1);

    // 2. listContexts
    runner.TestWithValidator("listContexts",
        [&ctx1](const json& resp) {
            if (!resp.contains("result") || !resp["result"].is_array()) return false;
            for (const auto& c : resp["result"]) {
                if (c.get<std::string>() == ctx1) return true;
            }
            return false;
        }, {}, CAT);

    // 3. releaseContext
    auto r3 = runner.TestExpectType("releaseContext", "Boolean", {{"context_id", ctx1}}, CAT);

    // Create a new context for remaining tests
    auto r_new = client.Send("createContext", {});
    if (ResponseValidator::ValidateContextId(r_new)) {
        runner.SetActiveContext(ResponseValidator::GetStringResult(r_new));
    }
}

// ============================================================================
// BROWSER NAVIGATION (7 methods)
// ============================================================================
void RunNavigationTests(TestRunner& runner, IPCClient& client, const std::string& test_url) {
    const std::string CAT = "navigation";
    std::string ctx = runner.GetActiveContext();

    // 4. navigate
    runner.Test("navigate", {{"context_id", ctx}, {"url", test_url}}, CAT);

    // Wait for page to load before continuing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 4a. navigate with wait_until parameter (waits for load event)
    runner.Test("navigate", {{"context_id", ctx}, {"url", test_url}, {"wait_until", "load"}, {"timeout", 30000}}, CAT);

    // 4b. navigate with wait_until=domcontentloaded
    runner.Test("navigate", {{"context_id", ctx}, {"url", test_url}, {"wait_until", "domcontentloaded"}, {"timeout", 30000}}, CAT);

    // 5. waitForNavigation - Returns ActionResult with URL, HTTP status, firewall detection
    runner.TestExpectType("waitForNavigation", "ActionResult", {{"context_id", ctx}, {"timeout", 5000}}, CAT);

    // 6. reload - returns ActionResult
    runner.TestExpectType("reload", "ActionResult", {{"context_id", ctx}}, CAT);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 7. goBack - returns ActionResult (navigation_failed if no history)
    runner.TestExpectType("goBack", "ActionResult", {{"context_id", ctx}}, CAT);

    // 8. goForward - returns ActionResult (navigation_failed if no forward history)
    runner.TestExpectType("goForward", "ActionResult", {{"context_id", ctx}}, CAT);

    // 9. canGoBack - returns Boolean (false if no back history)
    runner.TestExpectType("canGoBack", "Boolean", {{"context_id", ctx}}, CAT);

    // 10. canGoForward - returns Boolean (false if no forward history)
    runner.TestExpectType("canGoForward", "Boolean", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// ELEMENT INTERACTION (13 methods)
// ============================================================================
void RunElementInteractionTests(TestRunner& runner, IPCClient& client, const std::string& test_url) {
    const std::string CAT = "element_interaction";
    std::string ctx = runner.GetActiveContext();

    // Navigate to the test page (owl://user_form.html/ has rich form elements)
    client.Send("navigate", {{"context_id", ctx}, {"url", test_url}});
    WaitForPageLoad(client, ctx);

    // 11. click - click on the submit button
    runner.Test("click", {{"context_id", ctx}, {"selector", "#submitBtn"}}, CAT);

    // 12. type - type into firstName input field
    runner.TestExpectType("type", "ActionResult", {{"context_id", ctx}, {"selector", "#firstName"}, {"text", "John"}}, CAT);

    // 13. pick - select from country dropdown
    runner.TestExpectType("pick", "ActionResult", {{"context_id", ctx}, {"selector", "#country"}, {"value", "US"}}, CAT);

    // 14. pressKey
    runner.Test("pressKey", {{"context_id", ctx}, {"key", "Tab"}}, CAT);

    // 15. submitForm
    runner.Test("submitForm", {{"context_id", ctx}}, CAT);

    // 16. hover - hover over submit button
    runner.Test("hover", {{"context_id", ctx}, {"selector", "#submitBtn"}}, CAT);

    // 17. doubleClick - double click on input to select text
    runner.Test("doubleClick", {{"context_id", ctx}, {"selector", "#firstName"}}, CAT);

    // 18. rightClick - right click on form
    runner.Test("rightClick", {{"context_id", ctx}, {"selector", ".form"}}, CAT);

    // 19. clearInput - clear the firstName input
    runner.TestExpectType("clearInput", "ActionResult", {{"context_id", ctx}, {"selector", "#firstName"}}, CAT);

    // 20. selectAll - select all in email input
    runner.Test("selectAll", {{"context_id", ctx}, {"selector", "#email"}}, CAT);

    // 21. focus - focus on username field
    runner.Test("focus", {{"context_id", ctx}, {"selector", "#username"}}, CAT);

    // 22. blur - blur from username
    runner.Test("blur", {{"context_id", ctx}, {"selector", "#username"}}, CAT);

    // 23. keyboardCombo - Ctrl+A to select all
    runner.Test("keyboardCombo", {{"context_id", ctx}, {"combo", "Ctrl+A"}}, CAT);
}

// ============================================================================
// MOUSE & DRAG OPERATIONS (3 methods)
// Uses owl://canvas_test.html/ which has slider, puzzle, and reorder elements
// ============================================================================
void RunMouseDragTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "mouse_drag";
    std::string ctx = runner.GetActiveContext();

    // Navigate to canvas_test page which has drag elements
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://canvas_test.html/"}});
    WaitForPageLoad(client, ctx);

    // 24. dragDrop - drag the slider thumb from left to right
    // Slider track is ~300px wide, thumb starts at left
    runner.TestExpectType("dragDrop", "ActionResult",
        {{"context_id", ctx}, {"start_x", 50}, {"start_y", 250}, {"end_x", 350}, {"end_y", 250}}, CAT);

    // 25. html5DragDrop - reorder items (drag item "3" to position of item "1")
    // The reorder container has items with data-value="1|2|3|4"
    runner.TestExpectType("html5DragDrop", "ActionResult",
        {{"context_id", ctx}, {"source_selector", ".reorder-item[data-value=\"3\"]"}, {"target_selector", ".reorder-item[data-value=\"1\"]"}}, CAT);

    // 26. mouseMove - move cursor across the drawing canvas area
    runner.TestExpectType("mouseMove", "ActionResult",
        {{"context_id", ctx}, {"start_x", 100}, {"start_y", 500}, {"end_x", 300}, {"end_y", 600}, {"steps", 20}}, CAT);
}

// ============================================================================
// ELEMENT STATE & PROPERTIES (7 methods)
// Uses elements from owl://user_form.html/
// ============================================================================
void RunElementStateTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "element_state";
    std::string ctx = runner.GetActiveContext();

    // Navigate back to user_form for element state tests
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // 27. isVisible - check if submit button is visible
    runner.TestExpectType("isVisible", "ActionResult", {{"context_id", ctx}, {"selector", "#submitBtn"}}, CAT);

    // 28. isEnabled - check if firstName input is enabled
    runner.TestExpectType("isEnabled", "ActionResult", {{"context_id", ctx}, {"selector", "#firstName"}}, CAT);

    // 29. isChecked - check if newsletter checkbox is checked (should be unchecked initially)
    runner.TestExpectType("isChecked", "ActionResult", {{"context_id", ctx}, {"selector", "#newsletter"}}, CAT);

    // 30. getAttribute - get placeholder from email input
    runner.Test("getAttribute", {{"context_id", ctx}, {"selector", "#email"}, {"attribute", "placeholder"}}, CAT);

    // 31. getBoundingBox - get bounds of submit button
    runner.TestExpectType("getBoundingBox", "JSON", {{"context_id", ctx}, {"selector", "#submitBtn"}}, CAT);

    // 32. getElementAtPosition - click position should find form element
    runner.Test("getElementAtPosition", {{"context_id", ctx}, {"x", 400}, {"y", 300}}, CAT);

    // 33. getInteractiveElements - get all form controls
    runner.TestExpectType("getInteractiveElements", "JSON", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// JAVASCRIPT EVALUATION (1 method)
// ============================================================================
void RunJavaScriptTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "javascript";
    std::string ctx = runner.GetActiveContext();

    // 34. evaluate - execute JS statement (no return value expected)
    runner.Test("evaluate",
        {{"context_id", ctx}, {"script", "console.log('test')"}}, CAT);

    // 35. evaluate with return_value=true - returns actual JS value
    runner.TestExpectType("evaluate", "String",
        {{"context_id", ctx}, {"script", "document.title"}, {"return_value", true}}, CAT);

    // 36. evaluate with expression parameter - shorthand for return_value=true
    runner.TestExpectType("evaluate", "String",
        {{"context_id", ctx}, {"expression", "document.title"}}, CAT);
}

// ============================================================================
// CLIPBOARD MANAGEMENT (3 methods)
// ============================================================================
void RunClipboardTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "clipboard";
    std::string ctx = runner.GetActiveContext();

    // 36. clipboardWrite - write text to clipboard
    runner.Test("clipboardWrite",
        {{"context_id", ctx}, {"text", "Test clipboard content"}}, CAT);

    // 37. clipboardRead - read text from clipboard (returns JSON object with text field)
    runner.TestExpectType("clipboardRead", "JSON", {{"context_id", ctx}}, CAT);

    // 38. clipboardClear - clear clipboard
    runner.Test("clipboardClear", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// CONTENT EXTRACTION (6 methods)
// ============================================================================
void RunContentExtractionTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "content_extraction";
    std::string ctx = runner.GetActiveContext();

    // 35. extractText
    runner.TestExpectType("extractText", "String", {{"context_id", ctx}}, CAT);

    // 36. getHTML
    runner.TestExpectType("getHTML", "String", {{"context_id", ctx}, {"clean_level", "basic"}}, CAT);

    // 37. getMarkdown
    runner.TestExpectType("getMarkdown", "String",
        {{"context_id", ctx}, {"include_links", true}, {"include_images", true}}, CAT);

    // 38. extractJSON - may return String or JSON depending on page content
    runner.Test("extractJSON", {{"context_id", ctx}}, CAT);

    // 39. detectWebsiteType
    runner.TestExpectType("detectWebsiteType", "String", {{"context_id", ctx}}, CAT);

    // 40. listTemplates
    runner.TestExpectType("listTemplates", "JSON", {}, CAT);
}

// ============================================================================
// SCREENSHOT & VISUAL FEEDBACK (6 methods)
// Uses owl://user_form.html/ for screenshot tests
// ============================================================================
void RunScreenshotVisualTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "screenshot_visual";
    std::string ctx = runner.GetActiveContext();

    // Ensure we're on user_form for screenshots
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // 41. screenshot (viewport mode - default)
    runner.TestWithValidator("screenshot",
        [](const json& resp) {
            return ResponseValidator::ValidateBase64Image(resp);
        }, {{"context_id", ctx}}, CAT);

    // 41b. screenshot (viewport mode - explicit)
    runner.TestWithValidator("screenshot",
        [](const json& resp) {
            return ResponseValidator::ValidateBase64Image(resp);
        }, {{"context_id", ctx}, {"mode", "viewport"}}, CAT);

    // 41c. screenshot (element mode) - capture the registration form
    runner.TestWithValidator("screenshot",
        [](const json& resp) {
            return ResponseValidator::ValidateBase64Image(resp);
        }, {{"context_id", ctx}, {"mode", "element"}, {"selector", "#registrationForm"}}, CAT);

    // 41d. screenshot (fullpage mode) - capture entire scrollable page
    runner.TestWithValidator("screenshot",
        [](const json& resp) {
            return ResponseValidator::ValidateBase64Image(resp);
        }, {{"context_id", ctx}, {"mode", "fullpage"}}, CAT);

    // 42. highlight - highlight the submit button
    runner.TestExpectType("highlight", "ActionResult",
        {{"context_id", ctx}, {"selector", "#submitBtn"}, {"border_color", "#FF0000"}}, CAT);

    // 43. showGridOverlay - show position grid over form
    runner.TestExpectType("showGridOverlay", "ActionResult",
        {{"context_id", ctx}, {"horizontal_lines", 10}, {"vertical_lines", 10}}, CAT);
}

// ============================================================================
// SCROLLING OPERATIONS (5 methods)
// Uses owl://user_form.html/ which has a long form requiring scrolling
// ============================================================================
void RunScrollingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "scrolling";
    std::string ctx = runner.GetActiveContext();

    // Ensure we're on user_form (long scrollable page)
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // 44. scrollBy - scroll down 100px
    runner.TestExpectType("scrollBy", "ActionResult", {{"context_id", ctx}, {"x", 0}, {"y", 100}}, CAT);

    // 44a. scrollBy with verification_level - verifies scroll position
    runner.TestExpectType("scrollBy", "ActionResult", {{"context_id", ctx}, {"x", 0}, {"y", 50}, {"verification_level", "basic"}}, CAT);

    // 44b. scrollBy with strict verification
    runner.TestExpectType("scrollBy", "ActionResult", {{"context_id", ctx}, {"x", 0}, {"y", 50}, {"verification_level", "strict"}}, CAT);

    // 45. scrollTo - scroll back to top
    runner.TestExpectType("scrollTo", "ActionResult", {{"context_id", ctx}, {"x", 0}, {"y", 0}}, CAT);

    // 45a. scrollTo with verification_level
    runner.TestExpectType("scrollTo", "ActionResult", {{"context_id", ctx}, {"x", 0}, {"y", 100}, {"verification_level", "basic"}}, CAT);

    // 45b. scrollTo with strict verification
    runner.TestExpectType("scrollTo", "ActionResult", {{"context_id", ctx}, {"x", 0}, {"y", 0}, {"verification_level", "strict"}}, CAT);

    // 46. scrollToElement - scroll to submit button at bottom of form
    runner.Test("scrollToElement", {{"context_id", ctx}, {"selector", "#submitBtn"}}, CAT);

    // 47. scrollToTop - scroll back to top of page
    runner.Test("scrollToTop", {{"context_id", ctx}}, CAT);

    // 48. scrollToBottom - scroll to bottom of page
    runner.Test("scrollToBottom", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// WAIT & TIMING (5 methods)
// Uses elements from owl://user_form.html/
// ============================================================================
void RunWaitTimingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "wait_timing";
    std::string ctx = runner.GetActiveContext();

    // 49. waitForSelector - wait for the registration form
    runner.Test("waitForSelector", {{"context_id", ctx}, {"selector", "#registrationForm"}, {"timeout", 5000}}, CAT);

    // 50. waitForTimeout - simple delay
    runner.Test("waitForTimeout", {{"context_id", ctx}, {"timeout", 100}}, CAT);

    // 51. waitForNetworkIdle - wait for page to settle
    runner.Test("waitForNetworkIdle", {{"context_id", ctx}, {"idle_time", 500}, {"timeout", 10000}}, CAT);

    // 52. waitForFunction - check if form exists in DOM
    runner.Test("waitForFunction",
        {{"context_id", ctx}, {"js_function", "return document.getElementById('registrationForm') !== null;"}, {"polling", 100}, {"timeout", 5000}}, CAT);

    // 53. waitForURL - verify we're on user_form page
    runner.Test("waitForURL",
        {{"context_id", ctx}, {"url_pattern", "user_form"}, {"timeout", 5000}}, CAT);
}

// ============================================================================
// PAGE STATE QUERIES (3 methods)
// ============================================================================
void RunPageStateTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "page_state";
    std::string ctx = runner.GetActiveContext();

    // 54. getCurrentURL
    runner.TestExpectType("getCurrentURL", "String", {{"context_id", ctx}}, CAT);

    // 55. getPageTitle
    runner.TestExpectType("getPageTitle", "String", {{"context_id", ctx}}, CAT);

    // 56. getPageInfo
    runner.TestExpectType("getPageInfo", "JSON", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// VIEWPORT & DISPLAY (2 methods)
// ============================================================================
void RunViewportTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "viewport";
    std::string ctx = runner.GetActiveContext();

    // 57. setViewport - returns ActionResult
    runner.TestExpectType("setViewport", "ActionResult",
        {{"context_id", ctx}, {"width", 1280}, {"height", 720}}, CAT);

    // 58. getViewport
    runner.TestExpectType("getViewport", "JSON", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// VIDEO RECORDING (5 methods)
// ============================================================================
void RunVideoRecordingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "video_recording";
    std::string ctx = runner.GetActiveContext();

    // 59. startVideoRecording
    runner.TestExpectType("startVideoRecording", "Boolean",
        {{"context_id", ctx}, {"fps", 15}, {"codec", "libx264"}}, CAT);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 60. pauseVideoRecording
    runner.TestExpectType("pauseVideoRecording", "Boolean", {{"context_id", ctx}}, CAT);

    // 61. resumeVideoRecording
    runner.TestExpectType("resumeVideoRecording", "Boolean", {{"context_id", ctx}}, CAT);

    // 63. getVideoRecordingStats
    runner.TestExpectType("getVideoRecordingStats", "JSON", {{"context_id", ctx}}, CAT);

    // 62. stopVideoRecording
    runner.TestExpectType("stopVideoRecording", "String", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// LIVE STREAMING (5 methods)
// ============================================================================
void RunLiveStreamingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "live_streaming";
    std::string ctx = runner.GetActiveContext();

    // 64. startLiveStream
    runner.TestExpectType("startLiveStream", "JSON",
        {{"context_id", ctx}, {"fps", 15}, {"quality", 75}}, CAT);

    // 67. listLiveStreams
    runner.TestExpectType("listLiveStreams", "JSON", {}, CAT);

    // 66. getLiveStreamStats
    runner.TestExpectType("getLiveStreamStats", "JSON", {{"context_id", ctx}}, CAT);

    // 68. getLiveFrame
    runner.Test("getLiveFrame", {{"context_id", ctx}}, CAT);

    // 65. stopLiveStream
    runner.TestExpectType("stopLiveStream", "Boolean", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// CAPTCHA SOLVING (5 methods)
// Uses owl://user_form.html/ which has a custom SecureCheck CAPTCHA
// - #captchaCheck: "I'm not a robot" checkbox
// - #captchaChallenge: Image selection grid (shown after clicking checkbox)
// ============================================================================
void RunCaptchaTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "captcha";
    std::string ctx = runner.GetActiveContext();

    // Ensure we're on user_form page which has CAPTCHA
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // Scroll to captcha section to make it visible
    client.Send("scrollToElement", {{"context_id", ctx}, {"selector", "#captchaContainer"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 69. detectCaptcha - should detect the SecureCheck CAPTCHA
    runner.TestExpectType("detectCaptcha", "JSON", {{"context_id", ctx}}, CAT);

    // 70. classifyCaptcha - should classify as checkbox type initially
    runner.TestExpectType("classifyCaptcha", "JSON", {{"context_id", ctx}}, CAT);

    // 71. solveTextCaptcha - returns JSON (no text captcha on this page)
    runner.TestExpectType("solveTextCaptcha", "JSON", {{"context_id", ctx}, {"max_attempts", 1}}, CAT);

    // Click the captcha checkbox to potentially trigger image challenge
    client.Send("click", {{"context_id", ctx}, {"selector", "#captchaCheck"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 72. solveImageCaptcha - attempt to solve image selection CAPTCHA (expected ~3000ms)
    runner.Test("solveImageCaptcha",
        {{"context_id", ctx}, {"max_attempts", 1}, {"provider", "owl"}}, CAT, 3000.0);

    // 73. solveCaptcha - auto-detect and solve CAPTCHA (expected ~4000ms)
    runner.Test("solveCaptcha", {{"context_id", ctx}, {"max_attempts", 1}}, CAT, 4000.0);
}

// ============================================================================
// COOKIE MANAGEMENT (3 methods)
// Uses owl:// protocol URLs for internal page cookies
// ============================================================================
void RunCookieTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "cookies";
    std::string ctx = runner.GetActiveContext();

    // 75. setCookie - set a cookie for the owl:// internal pages
    runner.TestExpectType("setCookie", "ActionResult",
        {{"context_id", ctx}, {"url", "owl://user_form.html/"},
         {"name", "owl_test_cookie"}, {"value", "test_value_123"},
         {"same_site", "lax"}}, CAT);

    // 74. getCookies - retrieve cookies for the context
    runner.TestExpectType("getCookies", "JSON", {{"context_id", ctx}}, CAT);

    // 76. deleteCookies - delete the test cookie
    runner.TestExpectType("deleteCookies", "ActionResult",
        {{"context_id", ctx}, {"cookie_name", "owl_test_cookie"}}, CAT);
}

// ============================================================================
// PROXY & NETWORK (4 methods)
// ============================================================================
void RunProxyTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "proxy";
    std::string ctx = runner.GetActiveContext();

    // 77. setProxy (without actual proxy - test the method exists)
    runner.TestExpectType("setProxy", "Boolean",
        {{"context_id", ctx}, {"proxy_type", "http"}, {"proxy_host", "127.0.0.1"},
         {"proxy_port", 8080}, {"proxy_enabled", false}}, CAT);

    // 78. getProxyStatus
    runner.TestExpectType("getProxyStatus", "JSON", {{"context_id", ctx}}, CAT);

    // 79. connectProxy
    runner.TestExpectType("connectProxy", "Boolean", {{"context_id", ctx}}, CAT);

    // 80. disconnectProxy
    runner.TestExpectType("disconnectProxy", "Boolean", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// PROFILE MANAGEMENT (5 methods)
// ============================================================================
void RunProfileTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "profile";
    std::string ctx = runner.GetActiveContext();

    // 81. createProfile - may return Error if profile system not initialized
    runner.Test("createProfile", {{"name", "test_profile"}}, CAT);

    // 84. getProfile - may return Error if no profile loaded
    runner.Test("getProfile", {{"context_id", ctx}}, CAT);

    // 84b. getContextInfo - returns VM profile and fingerprint hashes
    runner.Test("getContextInfo", {{"context_id", ctx}}, CAT);

    // 85. updateProfileCookies - returns Error if no profile loaded
    runner.TestExpectError("updateProfileCookies", {{"context_id", ctx}}, CAT);

    // 83. saveProfile - may return Error without profile_path
    runner.Test("saveProfile", {{"context_id", ctx}, {"profile_path", "/tmp/test_profile.json"}}, CAT);

    // 82. loadProfile - skip actual load test without valid profile file
    // runner.Test("loadProfile", {{"context_id", ctx}, {"profile_path", "/tmp/test_profile.json"}}, CAT);
}

// ============================================================================
// FILE OPERATIONS (1 method)
// Note: Our static pages don't have file inputs, so this tests error handling
// ============================================================================
void RunFileTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "files";
    std::string ctx = runner.GetActiveContext();

    // 86. uploadFile - expects error since user_form doesn't have file input
    // Browser returns 'upload_failed' when file input element is not found
    runner.TestExpectStatus("uploadFile", "upload_failed",
        {{"context_id", ctx}, {"selector", "input[type=file]"}, {"file_paths", json::array({"/tmp/test.txt"})}}, CAT);
}

// ============================================================================
// IFRAME/FRAME MANAGEMENT (3 methods)
// ============================================================================
void RunFrameTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "frames";
    std::string ctx = runner.GetActiveContext();

    // 87. listFrames
    runner.TestExpectType("listFrames", "JSON", {{"context_id", ctx}}, CAT);

    // 88. switchToFrame - returns ActionResult
    runner.TestExpectType("switchToFrame", "ActionResult",
        {{"context_id", ctx}, {"frame_selector", "0"}}, CAT);

    // 89. switchToMainFrame - returns ActionResult
    runner.TestExpectType("switchToMainFrame", "ActionResult", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// NETWORK INTERCEPTION & LOGGING (6 methods)
// ============================================================================
void RunNetworkTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "network";
    std::string ctx = runner.GetActiveContext();

    // 92. enableNetworkInterception
    runner.TestExpectType("enableNetworkInterception", "Boolean",
        {{"context_id", ctx}, {"enable", true}}, CAT);

    // 93. enableNetworkLogging
    runner.TestExpectType("enableNetworkLogging", "Boolean",
        {{"context_id", ctx}, {"enable", true}}, CAT);

    // 90. addNetworkRule
    auto r90 = runner.TestExpectType("addNetworkRule", "JSON",
        {{"context_id", ctx}, {"rule_json", "{\"url_pattern\":\"*.ads.*\",\"action\":\"block\"}"}}, CAT);

    // 94. getNetworkLog
    runner.TestExpectType("getNetworkLog", "JSON", {{"context_id", ctx}}, CAT);

    // 95. clearNetworkLog
    runner.TestExpectType("clearNetworkLog", "Boolean", {{"context_id", ctx}}, CAT);

    // 91. removeNetworkRule
    if (r90.response.contains("result") && r90.response["result"].contains("rule_id")) {
        std::string rule_id = r90.response["result"]["rule_id"].get<std::string>();
        runner.TestExpectType("removeNetworkRule", "Boolean", {{"rule_id", rule_id}}, CAT);
    } else {
        runner.TestExpectType("removeNetworkRule", "Boolean", {{"rule_id", "test_rule"}}, CAT);
    }
}

// ============================================================================
// DOWNLOAD MANAGEMENT (5 methods)
// ============================================================================
void RunDownloadTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "downloads";
    std::string ctx = runner.GetActiveContext();

    // 96. setDownloadPath
    runner.TestExpectType("setDownloadPath", "Boolean",
        {{"context_id", ctx}, {"download_path", "/tmp"}}, CAT);

    // 97. getDownloads
    runner.TestExpectType("getDownloads", "JSON", {{"context_id", ctx}}, CAT);

    // 98. getActiveDownloads - may return Error if not implemented
    runner.Test("getActiveDownloads", {{"context_id", ctx}}, CAT);

    // 99. waitForDownload - expects Error since no active download exists
    runner.TestExpectError("waitForDownload", {{"download_id", "test_download"}, {"timeout", 100}}, CAT);

    // 100. cancelDownload - expects Error since no download exists
    runner.TestExpectError("cancelDownload", {{"download_id", "test_download"}}, CAT);
}

// ============================================================================
// DIALOG HANDLING (5 methods)
// ============================================================================
void RunDialogTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "dialogs";
    std::string ctx = runner.GetActiveContext();

    // 101. setDialogAction
    runner.TestExpectType("setDialogAction", "Boolean",
        {{"context_id", ctx}, {"dialog_type", "alert"}, {"action", "accept"}}, CAT);

    // 102. getPendingDialog - may return Error or null JSON if no dialog pending
    runner.Test("getPendingDialog", {{"context_id", ctx}}, CAT);

    // 105. getDialogs - may return Error or empty JSON if no dialogs
    runner.Test("getDialogs", {{"context_id", ctx}}, CAT);

    // 104. waitForDialog - expects Error/timeout since no dialog exists
    runner.TestExpectError("waitForDialog", {{"context_id", ctx}, {"timeout", 100}}, CAT);

    // 103. handleDialog - expects Error since no pending dialog
    runner.TestExpectError("handleDialog", {{"dialog_id", "test_dialog"}, {"accept", true}}, CAT);
}

// ============================================================================
// TAB/WINDOW MANAGEMENT (8 methods)
// ============================================================================
void RunTabTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "tabs";
    std::string ctx = runner.GetActiveContext();

    // 106. setPopupPolicy
    runner.TestExpectType("setPopupPolicy", "Boolean",
        {{"context_id", ctx}, {"popup_policy", "block"}}, CAT);

    // 107. getTabs
    runner.TestExpectType("getTabs", "JSON", {{"context_id", ctx}}, CAT);

    // 111. getActiveTab
    runner.TestExpectType("getActiveTab", "JSON", {{"context_id", ctx}}, CAT);

    // 112. getTabCount
    runner.TestExpectType("getTabCount", "JSON", {{"context_id", ctx}}, CAT);

    // 110. newTab
    auto r110 = runner.TestExpectType("newTab", "JSON", {{"context_id", ctx}}, CAT);

    std::string new_tab_id;
    if (r110.response.contains("result") && r110.response["result"].contains("tab_id")) {
        new_tab_id = r110.response["result"]["tab_id"].get<std::string>();
    }

    // 108. switchTab - returns ActionResult
    if (!new_tab_id.empty()) {
        runner.TestExpectType("switchTab", "ActionResult",
            {{"context_id", ctx}, {"tab_id", new_tab_id}}, CAT);
    }

    // 113. getBlockedPopups
    runner.TestExpectType("getBlockedPopups", "JSON", {{"context_id", ctx}}, CAT);

    // 109. closeTab - may return Error depending on tab state
    if (!new_tab_id.empty()) {
        runner.Test("closeTab", {{"context_id", ctx}, {"tab_id", new_tab_id}}, CAT);
    } else {
        // Test with placeholder tab_id to verify method exists
        runner.TestExpectError("closeTab", {{"context_id", ctx}, {"tab_id", "nonexistent_tab"}}, CAT);
    }
}

// ============================================================================
// AI & LLM FEATURES (9 methods)
// Uses owl://user_form.html/ elements for AI interaction tests
// Note: Many of these may return Error if LLM is not available/configured
// ============================================================================
void RunAILLMTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "ai_llm";
    std::string ctx = runner.GetActiveContext();

    // Ensure we're on user_form for AI tests
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // 116. getLLMStatus - check if LLM is available
    runner.Test("getLLMStatus", {}, CAT);

    // 114. summarizePage - summarize the registration form page (expected ~1500ms)
    runner.Test("summarizePage", {{"context_id", ctx}}, CAT, 1500.0);

    // 115. queryPage - ask about the form fields (expected ~1000ms)
    runner.Test("queryPage", {{"context_id", ctx}, {"query", "What form fields are available on this page?"}}, CAT, 1000.0);

    // 117. executeNLA - natural language action on the form (expected ~500ms)
    runner.Test("executeNLA", {{"context_id", ctx}, {"query", "click the submit button"}}, CAT, 500.0);

    // 118. aiClick - click on the create account button using AI
    runner.Test("aiClick", {{"context_id", ctx}, {"description", "create account button"}}, CAT);

    // 119. aiType - type into the first name field using AI
    runner.Test("aiType", {{"context_id", ctx}, {"description", "first name input"}, {"text", "John"}}, CAT);

    // 120. aiExtract - extract form labels and inputs
    runner.Test("aiExtract", {{"context_id", ctx}, {"what", "form field labels"}}, CAT);

    // 121. aiQuery - ask about the page title
    runner.Test("aiQuery", {{"context_id", ctx}, {"query", "What is the title of this registration form?"}}, CAT);

    // 122. aiAnalyze - analyze the page structure
    runner.Test("aiAnalyze", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// ELEMENT FINDING (2 methods)
// Uses owl://user_form.html/ for finding elements
// ============================================================================
void RunElementFindingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "element_finding";
    std::string ctx = runner.GetActiveContext();

    // 123. findElement - find input fields on the form
    runner.Test("findElement", {{"context_id", ctx}, {"description", "text input field"}, {"max_results", 5}}, CAT);

    // 124. getBlockerStats - get ad/tracker blocking statistics
    runner.TestExpectType("getBlockerStats", "JSON", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// CONTEXT & DEMOGRAPHICS (5 methods)
// ============================================================================
void RunDemographicsTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "demographics";

    // 125. getDemographics - may return Error if service unavailable
    runner.Test("getDemographics", {}, CAT);

    // 126. getLocation - may return Error if service unavailable
    runner.Test("getLocation", {}, CAT);

    // 127. getDateTime - may return Error if service unavailable
    runner.Test("getDateTime", {}, CAT);

    // 128. getWeather - may return Error if service unavailable
    runner.Test("getWeather", {}, CAT);

    // 129. getHomepage
    runner.TestExpectType("getHomepage", "String", {}, CAT);
}

// ============================================================================
// LICENSE & SYSTEM (6 methods)
// ============================================================================
void RunLicenseSystemTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "license_system";

    // 130. getLicenseStatus - read-only, safe to test
    runner.Test("getLicenseStatus", {}, CAT);

    // 131. getLicenseInfo - read-only, may return Error if no detailed info available
    runner.Test("getLicenseInfo", {}, CAT);

    // 132. getHardwareFingerprint - read-only, may return Error if not available
    runner.Test("getHardwareFingerprint", {}, CAT);

    // 133. addLicense - SKIPPED: Cannot test without risking license state
    // These methods modify license state and should NOT be called via IPC during testing
    std::cout << "[SKIP] addLicense - skipped to protect license state" << std::endl;

    // 134. removeLicense - SKIPPED: Cannot test without risking license removal
    // These methods modify license state and should NOT be called via IPC during testing
    std::cout << "[SKIP] removeLicense - skipped to protect license state" << std::endl;

    // 135. shutdown - DO NOT RUN in normal tests as it stops the browser
    // This is only tested at the very end if requested
}

// ============================================================================
// ERROR HANDLING TESTS
// Tests error conditions with invalid inputs and non-existent elements
// Note: Browser may return generic 'error' status instead of specific codes
// ============================================================================
void RunErrorHandlingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "error_handling";
    std::string ctx = runner.GetActiveContext();

    // Ensure we're on a known page for consistent error testing
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // Test invalid context_id - expect error response
    runner.TestExpectError("click",
        {{"context_id", "invalid_ctx_12345"}, {"selector", "#firstName"}}, CAT);

    // Navigate with invalid context returns ActionResult with browser_not_found status
    runner.TestExpectStatus("navigate", "browser_not_found",
        {{"context_id", "invalid_ctx_12345"}, {"url", "owl://user_form.html/"}}, CAT);

    // Test empty selector - expect error
    runner.TestExpectError("click", {{"context_id", ctx}, {"selector", ""}}, CAT);

    // Test non-existent element selector - expect element_not_found
    runner.TestExpectStatus("click", "element_not_found",
        {{"context_id", ctx}, {"selector", "#nonExistentElement12345"}}, CAT);

    // Test empty URL - expect error
    runner.TestExpectError("navigate", {{"context_id", ctx}, {"url", ""}}, CAT);

    // Test invalid method - expect error
    runner.TestExpectError("unknownMethod", {}, CAT);

    // Test empty js_function in waitForFunction - returns ActionResult with internal_error status
    runner.TestExpectStatus("waitForFunction", "internal_error",
        {{"context_id", ctx}, {"js_function", ""}, {"timeout", 100}}, CAT);

    // Test empty url_pattern in waitForURL - returns ActionResult with internal_error status
    runner.TestExpectStatus("waitForURL", "internal_error",
        {{"context_id", ctx}, {"url_pattern", ""}, {"timeout", 100}}, CAT);
}

// ============================================================================
// CONSOLE LOGGING (3 methods)
// ============================================================================
void RunConsoleLoggingTests(TestRunner& runner, IPCClient& client) {
    const std::string CAT = "console_logging";
    std::string ctx = runner.GetActiveContext();

    // Ensure we're on a page that can generate console logs
    client.Send("navigate", {{"context_id", ctx}, {"url", "owl://user_form.html/"}});
    WaitForPageLoad(client, ctx);

    // 136. enableConsoleLogging - enable console log capture
    runner.TestExpectType("enableConsoleLogging", "Boolean",
        {{"context_id", ctx}, {"enable", true}}, CAT);

    // Execute some JS that logs to console
    client.Send("evaluate", {{"context_id", ctx}, {"script", "console.log('test log message'); console.warn('test warning'); console.error('test error');"}});

    // Small delay to ensure logs are captured
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 137. getConsoleLogs - get captured console logs
    runner.TestExpectType("getConsoleLogs", "JSON", {{"context_id", ctx}}, CAT);

    // 138. clearConsoleLogs - clear captured console logs
    runner.TestExpectType("clearConsoleLogs", "Boolean", {{"context_id", ctx}}, CAT);
}

// ============================================================================
// MAIN TEST ENTRY POINT
// ============================================================================
bool RunAllMethodTests(TestRunner& runner, IPCClient& client, const std::string& test_url) {
    std::cout << "Running all 138 IPC method tests...\n" << std::endl;

    // Create initial context
    auto ctx_result = client.Send("createContext", {});
    if (!ResponseValidator::ValidateContextId(ctx_result)) {
        std::cerr << "Failed to create initial context!" << std::endl;
        return false;
    }
    runner.SetActiveContext(ResponseValidator::GetStringResult(ctx_result));

    // Navigate to test page
    std::string ctx = runner.GetActiveContext();
    client.Send("navigate", {{"context_id", ctx}, {"url", test_url}});
    WaitForPageLoad(client, ctx, 15000);

    // Run all test categories
    RunContextManagementTests(runner, client);
    RunNavigationTests(runner, client, test_url);
    RunElementInteractionTests(runner, client, test_url);
    RunMouseDragTests(runner, client);
    RunElementStateTests(runner, client);
    RunJavaScriptTests(runner, client);
    RunClipboardTests(runner, client);
    RunContentExtractionTests(runner, client);
    RunScreenshotVisualTests(runner, client);
    RunScrollingTests(runner, client);
    RunWaitTimingTests(runner, client);
    RunPageStateTests(runner, client);
    RunViewportTests(runner, client);
    RunVideoRecordingTests(runner, client);
    RunLiveStreamingTests(runner, client);
    RunCaptchaTests(runner, client);
    RunCookieTests(runner, client);
    RunProxyTests(runner, client);
    RunProfileTests(runner, client);
    RunFileTests(runner, client);
    RunFrameTests(runner, client);
    RunNetworkTests(runner, client);
    RunDownloadTests(runner, client);
    RunDialogTests(runner, client);
    RunTabTests(runner, client);
    RunAILLMTests(runner, client);
    RunElementFindingTests(runner, client);
    RunDemographicsTests(runner, client);
    RunLicenseSystemTests(runner, client);
    RunConsoleLoggingTests(runner, client);
    RunErrorHandlingTests(runner, client);

    // Cleanup
    client.Send("releaseContext", {{"context_id", runner.GetActiveContext()}});

    return runner.PrintSummary();
}
