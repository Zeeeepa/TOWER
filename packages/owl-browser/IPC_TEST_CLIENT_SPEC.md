# IPC Test Client Specification for Owl Browser

## CRITICAL: Test the BINARY DIRECTLY

**DO NOT** use MCP SDK or JavaScript wrappers. Test the **browser binary directly** via stdin/stdout JSON protocol.

```
Test Client (C++)  --stdin-->  owl_browser binary  --stdout-->  Test Client (C++)
     (JSON commands)                                    (JSON responses)
```

## Implementation Language: C++

The test client **MUST be implemented in C++** for the following reasons:
- Can be compiled and shipped as a binary inside production servers
- Direct process spawning with `fork()`/`exec()` or `popen()`
- Native JSON parsing (use nlohmann/json or similar)
- Low-level stdin/stdout pipe handling
- Can be integrated into CI/CD pipelines as a standalone executable

### C++ Implementation Requirements

```cpp
// Required headers
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <nlohmann/json.hpp>  // or include/json.hpp from project

// Key components to implement:
// 1. ProcessSpawner - spawn owl_browser with pipes for stdin/stdout
// 2. IPCClient - send JSON commands, receive JSON responses
// 3. TestRunner - execute tests, track pass/fail, measure timing
// 4. ResponseValidator - validate response types and ActionStatus codes
```

### Build Requirements

- **Compiler:** C++17 or later
- **JSON library:** nlohmann/json (already used in project) or rapidjson
- **Build system:** CMake (integrate with existing project build)
- **Output:** Single binary `ipc_test_client` that can run standalone

---

## Binary Location

```
macOS:  build/Release/owl_browser.app/Contents/MacOS/owl_browser
Linux:  build/owl_browser
```

---

## Protocol

### Startup
1. Spawn binary with `--instance-id <unique_id>`
2. Wait for `READY` on stdout
3. Start sending JSON commands

### Command Format (stdin)
```json
{"id": 1, "method": "createContext", "param1": "value1"}
```

### Response Types

There are **5 distinct response formats** returned by IPC commands:

#### 1. String Response (`SendResponse`)
```json
{"id": 1, "result": "ctx_000001"}
```

#### 2. Boolean Response (`SendBoolResponse`)
```json
{"id": 1, "result": true}
```

#### 3. Raw JSON Response (`SendRawJsonResponse`)
```json
{"id": 1, "result": {"key": "value", "count": 42}}
```

#### 4. ActionResult Response (`SendActionResult`)
```json
{
  "id": 1,
  "result": {
    "success": true,
    "status": "ok",
    "message": "Action completed successfully",
    "selector": "button#submit",
    "url": "https://example.com",
    "error_code": "",
    "http_status": 200,
    "element_count": 0
  }
}
```

#### 5. Error Response (`SendError`)
```json
{"id": 1, "error": "Unknown method: foo"}
```

---

## Complete ActionStatus Codes (28 total)

Source: `include/core/action_result.h`

### Success Statuses

| Code | String | Description |
|------|--------|-------------|
| `OK` | `"ok"` | Action completed successfully |

### Browser/Context Errors

| Code | String | Description |
|------|--------|-------------|
| `BROWSER_NOT_FOUND` | `"browser_not_found"` | Context ID doesn't exist or browser is closed |
| `BROWSER_NOT_READY` | `"browser_not_ready"` | Browser exists but not ready (initializing) |
| `CONTEXT_NOT_FOUND` | `"context_not_found"` | Context not found |

### Navigation Errors

| Code | String | Description |
|------|--------|-------------|
| `NAVIGATION_FAILED` | `"navigation_failed"` | Navigation failed (network error, timeout, etc.) |
| `NAVIGATION_TIMEOUT` | `"navigation_timeout"` | Navigation didn't complete in time |
| `PAGE_LOAD_ERROR` | `"page_load_error"` | Page failed to load (HTTP error, DNS error, etc.) |
| `REDIRECT_DETECTED` | `"redirect_detected"` | Page redirected to different URL |
| `CAPTCHA_DETECTED` | `"captcha_detected"` | Page appears to show a CAPTCHA |
| `FIREWALL_DETECTED` | `"firewall_detected"` | Web firewall/bot protection challenge detected |

### Element Interaction Errors

| Code | String | Description |
|------|--------|-------------|
| `ELEMENT_NOT_FOUND` | `"element_not_found"` | Element selector didn't match any element |
| `ELEMENT_NOT_VISIBLE` | `"element_not_visible"` | Element exists but is not visible |
| `ELEMENT_NOT_INTERACTABLE` | `"element_not_interactable"` | Element visible but cannot be interacted with |
| `ELEMENT_STALE` | `"element_stale"` | Element was found but is no longer in DOM |
| `MULTIPLE_ELEMENTS` | `"multiple_elements"` | Selector matched multiple elements (ambiguous) |

### Action Execution Errors

| Code | String | Description |
|------|--------|-------------|
| `CLICK_FAILED` | `"click_failed"` | Click action failed |
| `TYPE_FAILED` | `"type_failed"` | Type action failed - verification showed text not entered |
| `SCROLL_FAILED` | `"scroll_failed"` | Scroll action failed |
| `FOCUS_FAILED` | `"focus_failed"` | Focus action failed - verification showed element not focused |
| `BLUR_FAILED` | `"blur_failed"` | Blur action failed - verification showed element still focused |
| `CLEAR_FAILED` | `"clear_failed"` | Clear input failed - verification showed field still has content |

### Validation Errors

| Code | String | Description |
|------|--------|-------------|
| `INVALID_SELECTOR` | `"invalid_selector"` | Selector syntax is invalid |
| `INVALID_URL` | `"invalid_url"` | URL is malformed or not allowed |
| `INVALID_PARAMETER` | `"invalid_parameter"` | A parameter has invalid value |

### System Errors

| Code | String | Description |
|------|--------|-------------|
| `INTERNAL_ERROR` | `"internal_error"` | Unexpected internal error |
| `TIMEOUT` | `"timeout"` | Generic timeout |
| `NETWORK_TIMEOUT` | `"network_timeout"` | Network idle wait timed out |
| `WAIT_TIMEOUT` | `"wait_timeout"` | Wait condition not met in time |
| `UNKNOWN` | `"unknown"` | Unknown status |

---

## Parameter Validation Rules

### Validation Patterns in ProcessCommand

| Validation | Error Returned | Methods Using |
|------------|----------------|---------------|
| `context_id` → `GetBrowser()` returns nullptr | `BROWSER_NOT_FOUND` | All context-dependent methods |
| `selector.empty()` | `INVALID_SELECTOR` | click, type, hover, focus, blur, etc. |
| `url.empty()` | `INVALID_URL` | navigate |
| `js_function.empty()` | `INVALID_PARAMETER` ("js_function required") | waitForFunction |
| `url_pattern.empty()` | `INVALID_PARAMETER` ("url_pattern required") | waitForURL |
| `script.empty()` | `INTERNAL_ERROR` | evaluate |

### Default Values from ParseCommand

Source: `src/core/owl_subprocess.cc` lines 322-471

| Parameter | Default Value | Type |
|-----------|---------------|------|
| `timeout` | 30000 | int64 (ms) |
| `idle_time` | 500 | int64 (ms) |
| `polling` | 100 | int64 (ms) |
| `is_regex` | false | bool |
| `ignore_cache` | false | bool |
| `clean_level` | "basic" | string |
| `include_links` | true | bool |
| `include_images` | true | bool |
| `max_length` | -1 | int (no limit) |
| `force_refresh` | false | bool |
| `max_results` | 5 | int |
| `max_attempts` | 3 | int |
| `provider` | "auto" | string |
| `fps` | 30 | int |
| `quality` | 75 | int |
| `codec` | "libx264" | string |
| `horizontal_lines` | 25 | int |
| `vertical_lines` | 25 | int |
| `line_color` | "rgba(255,0,0,0.15)" | string |
| `text_color` | "rgba(255,0,0,0.4)" | string |
| `border_color` | "#FF0000" | string |
| `background_color` | "rgba(255,0,0,0.2)" | string |
| `steps` | 0 | int |
| `same_site` | "lax" | string |
| `expires` | -1 | int64 (session cookie) |
| `secure` | false | bool |
| `http_only` | false | bool |
| `llm_enabled` | true | bool |
| `llm_use_builtin` | true | bool |
| `llm_is_third_party` | false | bool |
| `proxy_enabled` | false | bool |
| `proxy_stealth` | true | bool |
| `proxy_block_webrtc` | true | bool |
| `proxy_spoof_timezone` | false | bool |
| `proxy_spoof_language` | false | bool |
| `proxy_trust_custom_ca` | false | bool |
| `is_tor` | false | bool |
| `tor_control_port` | -1 | int |

---

## COMPLETE IPC METHODS CATALOG

**Total Methods: 135**

Command dispatcher location: `src/core/owl_subprocess.cc` (ProcessCommand function, lines 605-1906)

---

### CONTEXT MANAGEMENT (3 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 1 | `createContext` | - | `llm_enabled` (bool, default: true), `llm_use_builtin` (bool, default: true), `llm_endpoint` (string), `llm_model` (string), `llm_api_key` (string), `llm_is_third_party` (bool, default: false), `proxy_type` (string), `proxy_host` (string), `proxy_port` (int), `proxy_username` (string), `proxy_password` (string), `proxy_enabled` (bool, default: false), `proxy_stealth` (bool, default: true), `proxy_block_webrtc` (bool, default: true), `proxy_spoof_timezone` (bool, default: false), `proxy_spoof_language` (bool, default: false), `proxy_timezone_override` (string), `proxy_language_override` (string), `proxy_ca_cert_path` (string), `proxy_trust_custom_ca` (bool, default: false), `is_tor` (bool, default: false), `tor_control_port` (int, default: -1), `tor_control_password` (string), `profile_path` (string) | String: `"ctx_000001"` | `INTERNAL_ERROR` (if creation fails) |
| 2 | `releaseContext` | `context_id` (string) | - | Boolean: `true/false` | None (always succeeds) |
| 3 | `listContexts` | - | - | Raw JSON: `["ctx_000001", "ctx_000002"]` | None |

---

### BROWSER NAVIGATION (7 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 4 | `navigate` | `context_id` (string), `url` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_URL` (empty url), `NAVIGATION_FAILED`, `NAVIGATION_TIMEOUT`, `PAGE_LOAD_ERROR`, `CAPTCHA_DETECTED`, `FIREWALL_DETECTED`, `REDIRECT_DETECTED` |
| 5 | `waitForNavigation` | `context_id` (string) | `timeout` (int, default: 30000ms) | ActionResult | `BROWSER_NOT_FOUND`, `NAVIGATION_TIMEOUT`, `FIREWALL_DETECTED`, `CAPTCHA_DETECTED` |
| 6 | `reload` | `context_id` (string) | `ignore_cache` (bool, default: false) | Boolean | `BROWSER_NOT_FOUND` |
| 7 | `goBack` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 8 | `goForward` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 9 | `canGoBack` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 10 | `canGoForward` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |

**Note:** `waitForNavigation` includes firewall detection (Cloudflare, Akamai, Imperva) at >=0.5 confidence.

---

### ELEMENT INTERACTION (13 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 11 | `click` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND`, `ELEMENT_NOT_VISIBLE`, `ELEMENT_NOT_INTERACTABLE`, `CLICK_FAILED` |
| 12 | `type` | `context_id` (string), `selector` (string, non-empty), `text` (string) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND`, `TYPE_FAILED` |
| 13 | `pick` | `context_id` (string), `selector` (string, non-empty), `value` (string) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND` |
| 14 | `pressKey` | `context_id` (string), `key` (string) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_PARAMETER` (invalid key) |
| 15 | `submitForm` | `context_id` (string) | - | ActionResult | `BROWSER_NOT_FOUND` |
| 16 | `hover` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND` |
| 17 | `doubleClick` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND` |
| 18 | `rightClick` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND` |
| 19 | `clearInput` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND`, `CLEAR_FAILED` |
| 20 | `selectAll` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND` |
| 21 | `focus` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND`, `FOCUS_FAILED` |
| 22 | `blur` | `context_id` (string), `selector` (string, non-empty) | - | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `ELEMENT_NOT_FOUND`, `BLUR_FAILED` |
| 23 | `keyboardCombo` | `context_id` (string), `combo` (string) | - | ActionResult | `BROWSER_NOT_FOUND` |

**Valid `key` values for `pressKey`:** Enter, Return, Tab, Escape, Esc, Backspace, Delete, Del, ArrowUp, Up, ArrowDown, Down, ArrowLeft, Left, ArrowRight, Right, Space, Home, End, PageUp, PageDown

**Combo examples for `keyboardCombo`:** "Ctrl+A", "Ctrl+C", "Ctrl+V", "Shift+Enter", "Ctrl+Shift+N", "Meta+V"

---

### MOUSE & DRAG OPERATIONS (3 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 24 | `dragDrop` | `context_id` (string), `start_x` (int), `start_y` (int), `end_x` (int), `end_y` (int) | `mid_points` (JSON array string: `"[[x1,y1],[x2,y2]]"`) | Boolean | `BROWSER_NOT_FOUND` |
| 25 | `html5DragDrop` | `context_id` (string), `source_selector` (string), `target_selector` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 26 | `mouseMove` | `context_id` (string), `start_x` (int), `start_y` (int), `end_x` (int), `end_y` (int) | `steps` (int, default: 0), `stop_points` (JSON array string) | Boolean | `BROWSER_NOT_FOUND` |

---

### ELEMENT STATE & PROPERTIES (7 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 27 | `isVisible` | `context_id` (string), `selector` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 28 | `isEnabled` | `context_id` (string), `selector` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 29 | `isChecked` | `context_id` (string), `selector` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 30 | `getAttribute` | `context_id` (string), `selector` (string), `attribute` (string) | - | String | `BROWSER_NOT_FOUND`, Error message if failed |
| 31 | `getBoundingBox` | `context_id` (string), `selector` (string) | - | Raw JSON: `{x, y, width, height}` | `BROWSER_NOT_FOUND`, Error message if element not found |
| 32 | `getElementAtPosition` | `context_id` (string), `x` (int), `y` (int) | - | Raw JSON | `BROWSER_NOT_FOUND` |
| 33 | `getInteractiveElements` | `context_id` (string) | - | Raw JSON: `[{elements}]` | `BROWSER_NOT_FOUND` |

---

### JAVASCRIPT EVALUATION (1 method)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 34 | `evaluate` | `context_id` (string), `script` (string, non-empty) | - | Raw JSON | `BROWSER_NOT_FOUND`, `INTERNAL_ERROR` (empty script) |

---

### CONTENT EXTRACTION (6 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 35 | `extractText` | `context_id` (string) | `selector` (string, default: "body") | String | `BROWSER_NOT_FOUND` |
| 36 | `getHTML` | `context_id` (string) | `clean_level` (string: "minimal"/"basic"/"aggressive", default: "basic") | String | `BROWSER_NOT_FOUND` |
| 37 | `getMarkdown` | `context_id` (string) | `include_links` (bool, default: true), `include_images` (bool, default: true), `max_length` (int, default: -1) | String | `BROWSER_NOT_FOUND` |
| 38 | `extractJSON` | `context_id` (string) | `template_name` (string), `custom_schema` (string) | Raw JSON | `BROWSER_NOT_FOUND` |
| 39 | `detectWebsiteType` | `context_id` (string) | - | String | `BROWSER_NOT_FOUND` |
| 40 | `listTemplates` | - | - | Raw JSON: `["google_search", "wikipedia", ...]` | None |

**Template names:** google_search, wikipedia, amazon_product, github_repo, twitter_feed, reddit_thread

---

### SCREENSHOT & VISUAL FEEDBACK (3 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 41 | `screenshot` | `context_id` (string) | - | String: base64 PNG | `BROWSER_NOT_FOUND` |
| 42 | `highlight` | `context_id` (string), `selector` (string) | `border_color` (string, default: "#FF0000"), `background_color` (string, default: "rgba(255,0,0,0.2)") | Boolean | `BROWSER_NOT_FOUND` |
| 43 | `showGridOverlay` | `context_id` (string) | `horizontal_lines` (int, default: 25), `vertical_lines` (int, default: 25), `line_color` (string, default: "rgba(255,0,0,0.15)"), `text_color` (string, default: "rgba(255,0,0,0.4)") | Boolean | `BROWSER_NOT_FOUND` |

---

### SCROLLING OPERATIONS (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 44 | `scrollBy` | `context_id` (string), `x` (int), `y` (int) | - | Boolean | `BROWSER_NOT_FOUND` |
| 45 | `scrollTo` | `context_id` (string), `x` (int), `y` (int) | - | Boolean | `BROWSER_NOT_FOUND` |
| 46 | `scrollToElement` | `context_id` (string), `selector` (string) | - | ActionResult | `BROWSER_NOT_FOUND`, `ELEMENT_NOT_FOUND`, `SCROLL_FAILED` |
| 47 | `scrollToTop` | `context_id` (string) | - | ActionResult | `BROWSER_NOT_FOUND`, `SCROLL_FAILED` |
| 48 | `scrollToBottom` | `context_id` (string) | - | ActionResult | `BROWSER_NOT_FOUND`, `SCROLL_FAILED` |

---

### WAIT & TIMING (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 49 | `waitForSelector` | `context_id` (string), `selector` (string, non-empty) | `timeout` (int, default: 5000ms) | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_SELECTOR`, `WAIT_TIMEOUT`, `ELEMENT_NOT_FOUND` |
| 50 | `waitForTimeout` | `context_id` (string) | `timeout` (int, default: 1000ms) | ActionResult | `BROWSER_NOT_FOUND` |
| 51 | `waitForNetworkIdle` | `context_id` (string) | `idle_time` (int, default: 500ms), `timeout` (int, default: 30000ms) | ActionResult | `BROWSER_NOT_FOUND`, `NETWORK_TIMEOUT` |
| 52 | `waitForFunction` | `context_id` (string), `js_function` (string, non-empty) | `polling` (int, default: 100ms), `timeout` (int, default: 30000ms) | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_PARAMETER` ("js_function required"), `WAIT_TIMEOUT` |
| 53 | `waitForURL` | `context_id` (string), `url_pattern` (string, non-empty) | `is_regex` (bool, default: false), `timeout` (int, default: 30000ms) | ActionResult | `BROWSER_NOT_FOUND`, `INVALID_PARAMETER` ("url_pattern required"), `WAIT_TIMEOUT` |

---

### PAGE STATE QUERIES (3 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 54 | `getCurrentURL` | `context_id` (string) | - | String | `BROWSER_NOT_FOUND` |
| 55 | `getPageTitle` | `context_id` (string) | - | String | `BROWSER_NOT_FOUND` |
| 56 | `getPageInfo` | `context_id` (string) | - | Raw JSON: `{url, title, http_status, navigation_state, can_go_back, can_go_forward}` | `BROWSER_NOT_FOUND` |

---

### VIEWPORT & DISPLAY (2 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 57 | `setViewport` | `context_id` (string), `width` (int), `height` (int) | - | Boolean | `BROWSER_NOT_FOUND` |
| 58 | `getViewport` | `context_id` (string) | - | Raw JSON: `{width, height}` | `BROWSER_NOT_FOUND` |

---

### VIDEO RECORDING (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 59 | `startVideoRecording` | `context_id` (string) | `fps` (int, default: 30), `codec` (string, default: "libx264") | Boolean | `BROWSER_NOT_FOUND` |
| 60 | `pauseVideoRecording` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 61 | `resumeVideoRecording` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 62 | `stopVideoRecording` | `context_id` (string) | - | String: file path | `BROWSER_NOT_FOUND` |
| 63 | `getVideoRecordingStats` | `context_id` (string) | - | Raw JSON: `{frames_captured, duration, fps}` | `BROWSER_NOT_FOUND` |

---

### LIVE STREAMING (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 64 | `startLiveStream` | `context_id` (string) | `fps` (int, default: 15, range: 1-60), `quality` (int, default: 75, range: 1-100) | Raw JSON: `{success, context_id, fps, quality, shm_name?, shm_available}` | `BROWSER_NOT_FOUND` |
| 65 | `stopLiveStream` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 66 | `getLiveStreamStats` | `context_id` (string) | - | Raw JSON | `BROWSER_NOT_FOUND` |
| 67 | `listLiveStreams` | - | - | Raw JSON: `[{streams}]` | None |
| 68 | `getLiveFrame` | `context_id` (string) | - | String: base64 JPEG OR Error | `BROWSER_NOT_FOUND` |

---

### CAPTCHA SOLVING (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 69 | `detectCaptcha` | `context_id` (string) | - | Raw JSON: `{detected, type, confidence, elements}` | `BROWSER_NOT_FOUND` |
| 70 | `classifyCaptcha` | `context_id` (string) | - | Raw JSON: `{type, provider, confidence}` | `BROWSER_NOT_FOUND` |
| 71 | `solveTextCaptcha` | `context_id` (string) | `max_attempts` (int, default: 3) | Raw JSON: `{success, solution}` | `BROWSER_NOT_FOUND` |
| 72 | `solveImageCaptcha` | `context_id` (string) | `max_attempts` (int, default: 3), `provider` (string: "auto"/"owl"/"recaptcha"/"cloudflare"/"hcaptcha", default: "auto") | Raw JSON: `{success}` | `BROWSER_NOT_FOUND` |
| 73 | `solveCaptcha` | `context_id` (string) | `max_attempts` (int, default: 3), `provider` (string, default: "auto") | Raw JSON: `{success}` | `BROWSER_NOT_FOUND` |

---

### COOKIE MANAGEMENT (3 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 74 | `getCookies` | `context_id` (string) | `url` (string, filter) | Raw JSON: `[{cookies}]` | `BROWSER_NOT_FOUND` |
| 75 | `setCookie` | `context_id` (string), `url` (string), `name` (string), `value` (string) | `domain` (string), `path` (string), `secure` (bool, default: false), `http_only` (bool, default: false), `same_site` (string: "none"/"lax"/"strict", default: "lax"), `expires` (int64 timestamp, default: -1 for session) | Boolean | `BROWSER_NOT_FOUND` |
| 76 | `deleteCookies` | `context_id` (string) | `url` (string), `cookie_name` (string) | Boolean | `BROWSER_NOT_FOUND` |

---

### PROXY & NETWORK (4 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 77 | `setProxy` | `context_id` (string) | `proxy_type` (string), `proxy_host` (string), `proxy_port` (int), `proxy_username` (string), `proxy_password` (string), `proxy_enabled` (bool), `proxy_stealth` (bool, default: true), `proxy_block_webrtc` (bool, default: true), `proxy_spoof_timezone` (bool, default: false), `proxy_spoof_language` (bool, default: false), `proxy_timezone_override` (string), `proxy_language_override` (string), `proxy_ca_cert_path` (string), `proxy_trust_custom_ca` (bool, default: false) | Boolean | `BROWSER_NOT_FOUND` |
| 78 | `getProxyStatus` | `context_id` (string) | - | Raw JSON: `{enabled, type, host, port, ...}` | `BROWSER_NOT_FOUND` |
| 79 | `connectProxy` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 80 | `disconnectProxy` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |

**Proxy types:** "http", "https", "socks4", "socks5", "socks5h" (socks5h uses remote DNS)

---

### PROFILE MANAGEMENT (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 81 | `createProfile` | - | `name` (string) | Raw JSON: profile object | None |
| 82 | `loadProfile` | `context_id` (string), `profile_path` (string) | - | Raw JSON: profile object | `BROWSER_NOT_FOUND`, Error if file not found |
| 83 | `saveProfile` | `context_id` (string) | `profile_path` (string) | Raw JSON: profile object | `BROWSER_NOT_FOUND` |
| 84 | `getProfile` | `context_id` (string) | - | Raw JSON: profile object | `BROWSER_NOT_FOUND` |
| 85 | `updateProfileCookies` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |

---

### FILE OPERATIONS (1 method)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 86 | `uploadFile` | `context_id` (string), `selector` (string), `file_paths` (JSON array string: `'["path1","path2"]'`) | - | Boolean | `BROWSER_NOT_FOUND` |

---

### IFRAME/FRAME MANAGEMENT (3 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 87 | `listFrames` | `context_id` (string) | - | Raw JSON: `[{frames}]` | `BROWSER_NOT_FOUND` |
| 88 | `switchToFrame` | `context_id` (string), `frame_selector` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 89 | `switchToMainFrame` | `context_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |

**Frame selector:** Can be frame name, frame index ("0", "1"), or CSS selector for iframe

---

### NETWORK INTERCEPTION & LOGGING (6 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 90 | `addNetworkRule` | `context_id` (string), `rule_json` (string) | - | Raw JSON: `{rule_id}` | `BROWSER_NOT_FOUND` |
| 91 | `removeNetworkRule` | `rule_id` (string) | - | Boolean | None |
| 92 | `enableNetworkInterception` | `context_id` (string), `enable` (bool) | - | Boolean: `true` | `BROWSER_NOT_FOUND` |
| 93 | `enableNetworkLogging` | `context_id` (string), `enable` (bool) | - | Boolean: `true` | `BROWSER_NOT_FOUND` |
| 94 | `getNetworkLog` | `context_id` (string) | - | Raw JSON: `[{requests}]` | `BROWSER_NOT_FOUND` |
| 95 | `clearNetworkLog` | `context_id` (string) | - | Boolean: `true` | `BROWSER_NOT_FOUND` |

**rule_json format:**
```json
{
  "url_pattern": "*.ads.example.com/*",
  "action": "block",
  "is_regex": false,
  "redirect_url": "",
  "mock_body": "",
  "mock_status": 200,
  "mock_content_type": "text/plain"
}
```
**Actions:** "block", "mock", "redirect", "allow"

---

### DOWNLOAD MANAGEMENT (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 96 | `setDownloadPath` | `context_id` (string), `download_path` (string) | - | Boolean: `true` | `BROWSER_NOT_FOUND` |
| 97 | `getDownloads` | `context_id` (string) | - | Raw JSON: `[{downloads}]` | `BROWSER_NOT_FOUND` |
| 98 | `getActiveDownloads` | `context_id` (string) | - | Raw JSON: `[{downloads}]` | `BROWSER_NOT_FOUND` |
| 99 | `waitForDownload` | `download_id` (string) | `timeout` (int, default: 30000ms) | Boolean | Error if download not found |
| 100 | `cancelDownload` | `download_id` (string) | - | Boolean | Error if download not found |

---

### DIALOG HANDLING (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 101 | `setDialogAction` | `context_id` (string), `dialog_type` (string), `action` (string) | `prompt_text` (string) | Boolean: `true` | `BROWSER_NOT_FOUND` |
| 102 | `getPendingDialog` | `context_id` (string) | - | Raw JSON: `{dialog_id, type, message, defaultValue, originUrl}` or `{}` | `BROWSER_NOT_FOUND` |
| 103 | `handleDialog` | `dialog_id` (string), `accept` (bool) | `response_text` (string) | Boolean | Error if dialog not found |
| 104 | `waitForDialog` | `context_id` (string) | `timeout` (int, default: 5000ms) | Boolean | `BROWSER_NOT_FOUND` |
| 105 | `getDialogs` | `context_id` (string) | - | Raw JSON: `[{dialogs}]` | `BROWSER_NOT_FOUND` |

**Dialog types:** "alert", "confirm", "prompt", "beforeunload"
**Actions:** "accept", "dismiss", "accept_with_text"

---

### TAB/WINDOW MANAGEMENT (8 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 106 | `setPopupPolicy` | `context_id` (string), `popup_policy` (string) | - | Boolean: `true` | `BROWSER_NOT_FOUND` |
| 107 | `getTabs` | `context_id` (string) | - | Raw JSON: `[{tab_id, url, is_main, is_popup, is_active}]` | `BROWSER_NOT_FOUND` |
| 108 | `switchTab` | `context_id` (string), `tab_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 109 | `closeTab` | `context_id` (string), `tab_id` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 110 | `newTab` | `context_id` (string) | `url` (string) | Raw JSON: `{tab_id}` | `BROWSER_NOT_FOUND` |
| 111 | `getActiveTab` | `context_id` (string) | - | Raw JSON: `{tab_id}` | `BROWSER_NOT_FOUND` |
| 112 | `getTabCount` | `context_id` (string) | - | Raw JSON: `{count}` | `BROWSER_NOT_FOUND` |
| 113 | `getBlockedPopups` | `context_id` (string) | - | Raw JSON: `[{popup URLs}]` | `BROWSER_NOT_FOUND` |

**Popup policies:** "allow", "block", "new_tab", "background"

---

### AI & LLM FEATURES (9 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 114 | `summarizePage` | `context_id` (string) | `force_refresh` (bool, default: false) | String | `BROWSER_NOT_FOUND` |
| 115 | `queryPage` | `context_id` (string), `query` (string) | - | String | `BROWSER_NOT_FOUND` |
| 116 | `getLLMStatus` | - | - | String: "ready"/"loading"/"unavailable" | None |
| 117 | `executeNLA` | `context_id` (string), `query` (string) | - | String | `BROWSER_NOT_FOUND` |
| 118 | `aiClick` | `context_id` (string), `description` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 119 | `aiType` | `context_id` (string), `description` (string), `text` (string) | - | Boolean | `BROWSER_NOT_FOUND` |
| 120 | `aiExtract` | `context_id` (string), `what` (string) | - | String | `BROWSER_NOT_FOUND` |
| 121 | `aiQuery` | `context_id` (string), `query` (string) | - | String | `BROWSER_NOT_FOUND` |
| 122 | `aiAnalyze` | `context_id` (string) | - | Raw JSON | `BROWSER_NOT_FOUND` |

---

### ELEMENT FINDING (2 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 123 | `findElement` | `context_id` (string), `description` (string) | `max_results` (int, default: 5) | Raw JSON | `BROWSER_NOT_FOUND` |
| 124 | `getBlockerStats` | `context_id` (string) | - | Raw JSON: `{ads_blocked, analytics_blocked, trackers_blocked}` | `BROWSER_NOT_FOUND` |

---

### CONTEXT & DEMOGRAPHICS (5 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 125 | `getDemographics` | - | - | Raw JSON: `{location, time, weather}` | None |
| 126 | `getLocation` | - | - | Raw JSON: `{city, country, coordinates, timezone}` | None |
| 127 | `getDateTime` | - | - | Raw JSON: `{date, time, day_of_week, timezone}` | None |
| 128 | `getWeather` | - | - | Raw JSON: `{temperature, condition, humidity, wind}` | None |
| 129 | `getHomepage` | - | - | String: escaped HTML | None |

---

### LICENSE & SYSTEM (6 methods)

| # | Method | Required Params | Optional Params | Response Type | Possible Errors |
|---|--------|-----------------|-----------------|---------------|-----------------|
| 130 | `getLicenseStatus` | - | - | Raw JSON: `{status, valid}` | None |
| 131 | `getLicenseInfo` | - | - | Raw JSON | None |
| 132 | `getHardwareFingerprint` | - | - | Raw JSON: `{fingerprint}` | None |
| 133 | `addLicense` | - | `license_path` (string) OR `license_data` (string, base64) | Raw JSON: `{success, status, license?, error?}` | None |
| 134 | `removeLicense` | - | - | Raw JSON: `{success, message}` | None |
| 135 | `shutdown` | - | - | String: "shutdown" | None |

---

## Key Source Files

| File | Purpose |
|------|---------|
| `src/core/owl_subprocess.cc` | **Main command dispatcher** - ProcessCommand function (lines 605-1906), ParseCommand (lines 322-471) |
| `include/core/action_result.h` | ActionStatus enum (28 codes), ActionResult struct with ToJSON() |
| `src/core/owl_browser_manager.cc` | Browser/context management, method implementations |
| `src/core/owl_client.cc` | Client handler, navigation, paint callbacks |
| `src/core/owl_ipc_server.cc` | IPC server (Linux multi-socket) |
| `src/automation/*.cc` | Element interaction, DOM scanning |
| `src/network/*.cc` | Proxy, cookies, network interception |
| `src/content/*.cc` | Content extraction, markdown conversion |
| `src/captcha/*.cc` | CAPTCHA detection/solving |
| `src/media/*.cc` | Video recording, live streaming |
| `src/ai/*.cc` | LLM features, NLA, semantic matching |
| `src/profile/*.cc` | Profile/fingerprint management |

---

## Error Response Patterns

### Pattern 1: SendError (Protocol-level error)
```json
{"id": 1, "error": "Unknown method: foobar"}
```
**When:** Invalid method name, malformed JSON, missing method field

### Pattern 2: ActionResult with success=false
```json
{
  "id": 1,
  "result": {
    "success": false,
    "status": "element_not_found",
    "message": "Element not found: #nonexistent",
    "selector": "#nonexistent"
  }
}
```
**When:** Element operations fail (click, type, hover, focus, scroll, wait)

### Pattern 3: Boolean false
```json
{"id": 1, "result": false}
```
**When:** Boolean operations fail (goBack with no history, reload fails)

### Pattern 4: Error in result string
```json
{"id": 1, "result": "Error: Invalid script"}
```
**When:** Some string-returning methods encounter errors

---

## Test Categories

### 1. Smoke Tests (Critical Path)
```
createContext → navigate → waitForNavigation → extractText → screenshot → releaseContext
```

### 2. Full Functionality Tests
- Test ALL 135 methods
- Valid parameters
- Expected response fields
- Correct response types

### 3. Error Handling Tests

| Test Case | Expected Response |
|-----------|-------------------|
| Invalid `context_id` | `BROWSER_NOT_FOUND` |
| Empty `selector` | `INVALID_SELECTOR` |
| Empty `url` in navigate | `INVALID_URL` |
| Empty `js_function` in waitForFunction | `INVALID_PARAMETER` |
| Empty `url_pattern` in waitForURL | `INVALID_PARAMETER` |
| Unknown method name | `{"error": "Unknown method: xxx"}` |
| Malformed JSON | `{"error": "..."}` |
| Element not found | `ELEMENT_NOT_FOUND` |
| Timeout exceeded | `WAIT_TIMEOUT`, `NETWORK_TIMEOUT`, `NAVIGATION_TIMEOUT` |

### 4. Stress Tests
- 100+ concurrent contexts
- 10000 rapid commands
- Memory leak detection
- Context cleanup verification

### 5. Regression Tests
- Known failure scenarios
- Edge cases
- Timeout handling

---

## C++ Test Client Architecture

### File Structure

```
tests/ipc/
├── CMakeLists.txt           # Build configuration
├── main.cc                  # Entry point
├── ipc_client.h             # IPC client class header
├── ipc_client.cc            # IPC client implementation
├── test_runner.h            # Test runner class header
├── test_runner.cc           # Test runner implementation
├── response_validator.h     # Response validation utilities
├── response_validator.cc    # Response validation implementation
└── tests/
    ├── context_tests.cc     # Context management tests
    ├── navigation_tests.cc  # Navigation tests
    ├── element_tests.cc     # Element interaction tests
    ├── content_tests.cc     # Content extraction tests
    └── ...                  # One file per category
```

### Core Classes

```cpp
// ipc_client.h
class IPCClient {
public:
    IPCClient(const std::string& browser_path);
    ~IPCClient();

    bool Start(const std::string& instance_id = "");
    void Stop();

    // Send command and wait for response
    nlohmann::json Send(const std::string& method, const nlohmann::json& params = {});

    // Get last response time in milliseconds
    double GetLastResponseTimeMs() const;

private:
    std::string browser_path_;
    pid_t child_pid_ = -1;
    int stdin_pipe_[2];
    int stdout_pipe_[2];
    int command_id_ = 0;
    double last_response_time_ms_ = 0;

    bool WaitForReady(int timeout_ms = 5000);
    std::string ReadLine();
    void WriteLine(const std::string& line);
};

// test_runner.h
struct TestResult {
    std::string method;
    bool success;
    double duration_ms;
    nlohmann::json request;
    nlohmann::json response;
    std::string error;
};

class TestRunner {
public:
    TestRunner(IPCClient& client);

    // Run a test expecting success
    TestResult Test(const std::string& method, const nlohmann::json& params = {});

    // Run a test expecting a specific error status
    TestResult TestError(const std::string& method, const std::string& expected_status,
                         const nlohmann::json& params = {});

    // Get all results
    const std::vector<TestResult>& GetResults() const;

    // Print summary and return true if all passed
    bool Report();

private:
    IPCClient& client_;
    std::vector<TestResult> results_;
};

// response_validator.h
class ResponseValidator {
public:
    // Validate response matches expected type
    static bool IsStringResponse(const nlohmann::json& response);
    static bool IsBoolResponse(const nlohmann::json& response);
    static bool IsJsonResponse(const nlohmann::json& response);
    static bool IsActionResult(const nlohmann::json& response);
    static bool IsErrorResponse(const nlohmann::json& response);

    // Validate ActionResult status
    static bool HasStatus(const nlohmann::json& response, const std::string& status);
    static std::string GetStatus(const nlohmann::json& response);

    // Valid ActionStatus codes
    static const std::vector<std::string> VALID_STATUS_CODES;
};
```

### Usage Example

```cpp
// main.cc
int main(int argc, char* argv[]) {
    std::string browser_path = GetBrowserPath();  // Detect macOS vs Linux

    IPCClient client(browser_path);
    if (!client.Start()) {
        std::cerr << "[FATAL] Failed to start browser" << std::endl;
        return 1;
    }

    TestRunner runner(client);

    // Create context
    auto ctx_result = runner.Test("createContext");
    std::string ctx = ctx_result.response.get<std::string>();

    // Error handling tests
    runner.TestError("click", "browser_not_found", {{"context_id", "invalid"}, {"selector", "body"}});
    runner.TestError("click", "invalid_selector", {{"context_id", ctx}, {"selector", ""}});
    runner.TestError("navigate", "invalid_url", {{"context_id", ctx}, {"url", ""}});

    // Navigation tests
    runner.Test("navigate", {{"context_id", ctx}, {"url", "https://example.com"}});
    runner.Test("waitForNavigation", {{"context_id", ctx}, {"timeout", 10000}});
    runner.Test("getCurrentURL", {{"context_id", ctx}});

    // ... test all 135 methods ...

    // Cleanup
    runner.Test("releaseContext", {{"context_id", ctx}});

    client.Stop();
    return runner.Report() ? 0 : 1;
}
```

### Command Line Interface

```bash
# Run all tests
./ipc_test_client

# Run with custom browser path
./ipc_test_client --browser-path /path/to/owl_browser

# Run specific test category
./ipc_test_client --category navigation

# Run with verbose output
./ipc_test_client --verbose

# Output JSON report with full benchmarking data
./ipc_test_client --json-report results.json

# Output HTML report (visual dashboard)
./ipc_test_client --html-report report.html

# Run stress tests
./ipc_test_client --stress --contexts 100 --commands 10000

# Run benchmarks only (no assertions, just timing)
./ipc_test_client --benchmark --iterations 100
```

---

## Benchmarking & Reporting

The test client MUST capture comprehensive metrics for solid benchmarking.

### Metrics to Capture

#### 1. Per-Command Timing
```cpp
struct CommandMetrics {
    std::string method;
    double latency_ms;           // Time from send to response received
    double parse_time_ms;        // JSON parse time
    int64_t request_size_bytes;  // Size of JSON request
    int64_t response_size_bytes; // Size of JSON response
    bool success;
    std::string status;          // ActionStatus code if applicable
};
```

#### 2. Process Resource Usage
```cpp
struct ProcessMetrics {
    // Memory
    int64_t rss_bytes;           // Resident Set Size
    int64_t vms_bytes;           // Virtual Memory Size
    int64_t heap_bytes;          // Heap usage (if available)

    // CPU
    double cpu_user_time_sec;    // User CPU time
    double cpu_system_time_sec;  // System CPU time
    double cpu_percent;          // CPU percentage

    // I/O
    int64_t read_bytes;          // Bytes read
    int64_t write_bytes;         // Bytes written

    // Timing
    int64_t timestamp_ms;        // When sample was taken
};
```

#### 3. Aggregated Statistics
```cpp
struct BenchmarkStats {
    // Latency stats (in milliseconds)
    double min_latency;
    double max_latency;
    double avg_latency;
    double median_latency;
    double p95_latency;          // 95th percentile
    double p99_latency;          // 99th percentile
    double stddev_latency;

    // Throughput
    double commands_per_second;
    double bytes_per_second;

    // Resource peaks
    int64_t peak_memory_bytes;
    double peak_cpu_percent;

    // Totals
    int total_commands;
    int successful_commands;
    int failed_commands;
    double total_duration_sec;
};
```

### Resource Monitoring Implementation

```cpp
// resource_monitor.h
class ResourceMonitor {
public:
    ResourceMonitor(pid_t target_pid);

    // Start background monitoring thread
    void Start(int sample_interval_ms = 100);
    void Stop();

    // Get current snapshot
    ProcessMetrics GetCurrentMetrics();

    // Get all samples
    std::vector<ProcessMetrics> GetAllSamples();

    // Get peak values
    ProcessMetrics GetPeakMetrics();

private:
    pid_t target_pid_;
    std::thread monitor_thread_;
    std::atomic<bool> running_;
    std::vector<ProcessMetrics> samples_;
    std::mutex samples_mutex_;

    // Platform-specific implementations
    ProcessMetrics ReadProcStat();      // Linux: /proc/[pid]/stat
    ProcessMetrics ReadMachTaskInfo();  // macOS: task_info()
};
```

### JSON Report Format

```json
{
  "metadata": {
    "test_run_id": "uuid-here",
    "timestamp": "2025-01-15T10:30:00Z",
    "browser_version": "1.0.0",
    "browser_path": "/path/to/owl_browser",
    "platform": "darwin",
    "platform_version": "14.0",
    "cpu_model": "Apple M1",
    "total_memory_gb": 16
  },
  "summary": {
    "total_tests": 135,
    "passed": 130,
    "failed": 5,
    "skipped": 0,
    "total_duration_sec": 45.2,
    "commands_per_second": 298.5
  },
  "latency_stats": {
    "min_ms": 0.5,
    "max_ms": 2500.0,
    "avg_ms": 15.3,
    "median_ms": 8.2,
    "p95_ms": 45.0,
    "p99_ms": 120.0,
    "stddev_ms": 25.1
  },
  "resource_stats": {
    "peak_memory_mb": 450,
    "avg_memory_mb": 320,
    "peak_cpu_percent": 85.0,
    "avg_cpu_percent": 35.0
  },
  "by_category": {
    "context_management": {
      "total": 3,
      "passed": 3,
      "avg_latency_ms": 250.0
    },
    "navigation": {
      "total": 7,
      "passed": 7,
      "avg_latency_ms": 1200.0
    }
    // ... other categories
  },
  "commands": [
    {
      "method": "createContext",
      "params": {},
      "success": true,
      "latency_ms": 245.3,
      "response_size_bytes": 28,
      "status": null,
      "memory_before_mb": 100,
      "memory_after_mb": 150
    }
    // ... all 135+ commands
  ],
  "resource_timeline": [
    {"timestamp_ms": 0, "memory_mb": 100, "cpu_percent": 5.0},
    {"timestamp_ms": 100, "memory_mb": 150, "cpu_percent": 45.0}
    // ... samples every 100ms
  ],
  "failures": [
    {
      "method": "click",
      "params": {"context_id": "ctx_1", "selector": "#missing"},
      "expected": "success",
      "actual": "element_not_found",
      "message": "Element not found: #missing"
    }
  ]
}
```

### HTML Report Features

The HTML report should include:

1. **Summary Dashboard**
   - Pass/fail pie chart
   - Overall metrics at a glance
   - Duration and throughput

2. **Latency Charts**
   - Histogram of latency distribution
   - Line chart of latency over time
   - Per-category bar chart

3. **Resource Usage Charts**
   - Memory usage over time (line chart)
   - CPU usage over time (line chart)
   - Peak vs average comparison

4. **Method Details Table**
   - Sortable/filterable table of all methods
   - Columns: Method, Category, Status, Latency, Memory Delta
   - Click to expand for full request/response

5. **Failure Analysis**
   - List of all failures with details
   - Expected vs actual comparison
   - Stack traces if available

### Benchmark Modes

```bash
# Quick smoke test (critical path only)
./ipc_test_client --mode smoke

# Full test suite (all 135 methods)
./ipc_test_client --mode full

# Benchmark mode (repeat each method N times, no assertions)
./ipc_test_client --mode benchmark --iterations 100

# Stress test (multiple contexts, rapid commands)
./ipc_test_client --mode stress --contexts 50 --duration 60

# Memory leak detection (long running)
./ipc_test_client --mode leak-check --duration 300 --sample-interval 1000

# Latency profiling (detailed timing breakdown)
./ipc_test_client --mode latency-profile --iterations 1000
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |
| 2 | Browser failed to start |
| 3 | Invalid arguments |
| 4 | Report generation failed |
| 5 | Timeout (browser hung) |

---

## Complete Method List by Category (135 total)

### Quick Reference

| Category | Count | Methods |
|----------|-------|---------|
| Context Management | 3 | createContext, releaseContext, listContexts |
| Navigation | 7 | navigate, waitForNavigation, reload, goBack, goForward, canGoBack, canGoForward |
| Element Interaction | 13 | click, type, pick, pressKey, submitForm, hover, doubleClick, rightClick, clearInput, selectAll, focus, blur, keyboardCombo |
| Mouse/Drag | 3 | dragDrop, html5DragDrop, mouseMove |
| Element State | 7 | isVisible, isEnabled, isChecked, getAttribute, getBoundingBox, getElementAtPosition, getInteractiveElements |
| JavaScript | 1 | evaluate |
| Content Extraction | 6 | extractText, getHTML, getMarkdown, extractJSON, detectWebsiteType, listTemplates |
| Screenshot/Visual | 3 | screenshot, highlight, showGridOverlay |
| Scrolling | 5 | scrollBy, scrollTo, scrollToElement, scrollToTop, scrollToBottom |
| Wait/Timing | 5 | waitForSelector, waitForTimeout, waitForNetworkIdle, waitForFunction, waitForURL |
| Page State | 3 | getCurrentURL, getPageTitle, getPageInfo |
| Viewport | 2 | setViewport, getViewport |
| Video Recording | 5 | startVideoRecording, pauseVideoRecording, resumeVideoRecording, stopVideoRecording, getVideoRecordingStats |
| Live Streaming | 5 | startLiveStream, stopLiveStream, getLiveStreamStats, listLiveStreams, getLiveFrame |
| CAPTCHA | 5 | detectCaptcha, classifyCaptcha, solveTextCaptcha, solveImageCaptcha, solveCaptcha |
| Cookies | 3 | getCookies, setCookie, deleteCookies |
| Proxy | 4 | setProxy, getProxyStatus, connectProxy, disconnectProxy |
| Profile | 5 | createProfile, loadProfile, saveProfile, getProfile, updateProfileCookies |
| Files | 1 | uploadFile |
| Frames | 3 | listFrames, switchToFrame, switchToMainFrame |
| Network | 6 | addNetworkRule, removeNetworkRule, enableNetworkInterception, enableNetworkLogging, getNetworkLog, clearNetworkLog |
| Downloads | 5 | setDownloadPath, getDownloads, getActiveDownloads, waitForDownload, cancelDownload |
| Dialogs | 5 | setDialogAction, getPendingDialog, handleDialog, waitForDialog, getDialogs |
| Tabs | 8 | setPopupPolicy, getTabs, switchTab, closeTab, newTab, getActiveTab, getTabCount, getBlockedPopups |
| AI/LLM | 9 | summarizePage, queryPage, getLLMStatus, executeNLA, aiClick, aiType, aiExtract, aiQuery, aiAnalyze |
| Element Finding | 2 | findElement, getBlockerStats |
| Demographics | 5 | getDemographics, getLocation, getDateTime, getWeather, getHomepage |
| License/System | 6 | getLicenseStatus, getLicenseInfo, getHardwareFingerprint, addLicense, removeLicense, shutdown |

**TOTAL: 135 methods**

---

## Success Criteria

1. **ALL 135 methods return valid responses**
2. **Response format matches spec exactly** (5 response types)
3. **ActionStatus codes match spec** (28 status codes)
4. **No crashes or hangs**
5. **Memory cleanup works** (context release)
6. **Clear error messages on failures**
7. **Required params validated** (empty checks return correct status)
8. **Default values applied correctly**
9. **All timeouts respected**
10. **Concurrent context support verified**

---

## Benchmarking Metrics

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Cold start | <2s | Time from spawn to READY |
| Context creation | <500ms | Time for createContext response |
| Navigation | <100ms | Time for navigate response (async start) |
| Screenshot | <500ms | Time for screenshot response |
| Element click | <200ms | Time for click response |
| Text extraction | <300ms | Time for extractText response |
| Memory per context | <100MB | Monitor RSS before/after context creation |
| Max concurrent contexts | 50+ | Create until failure/slowdown |

---

## Test Environment Requirements

- **Browser binary:** Built with Release configuration
- **C++ Compiler:** Clang or GCC with C++17 support
- **Build tools:** CMake 3.16+
- **JSON library:** nlohmann/json (included in project)
- **Network access** for navigation tests
- **Disk space:** ~500MB for video recording tests
- **Memory:** 8GB+ recommended for stress tests

### Building the Test Client

```bash
# From project root
mkdir -p build && cd build
cmake .. -DBUILD_IPC_TESTS=ON
make ipc_test_client

# Output binary location
# macOS: build/tests/ipc/ipc_test_client
# Linux: build/tests/ipc/ipc_test_client
```
