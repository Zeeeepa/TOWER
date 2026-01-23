/**
 * Type definitions for Owl Browser SDK v2.
 *
 * This module contains all type definitions, interfaces, and enums
 * using strict TypeScript types.
 */
// ==================== ENUMS ====================
/**
 * Authentication mode for remote HTTP server.
 */
export var AuthMode;
(function (AuthMode) {
    AuthMode["TOKEN"] = "token";
    AuthMode["JWT"] = "jwt";
})(AuthMode || (AuthMode = {}));
/**
 * Transport mode for remote connections.
 */
export var TransportMode;
(function (TransportMode) {
    TransportMode["HTTP"] = "http";
    TransportMode["WEBSOCKET"] = "websocket";
})(TransportMode || (TransportMode = {}));
/**
 * Condition operators for comparing values in flow conditions.
 */
export var ConditionOperator;
(function (ConditionOperator) {
    ConditionOperator["EQUALS"] = "equals";
    ConditionOperator["NOT_EQUALS"] = "not_equals";
    ConditionOperator["CONTAINS"] = "contains";
    ConditionOperator["NOT_CONTAINS"] = "not_contains";
    ConditionOperator["STARTS_WITH"] = "starts_with";
    ConditionOperator["ENDS_WITH"] = "ends_with";
    ConditionOperator["GREATER_THAN"] = "greater_than";
    ConditionOperator["LESS_THAN"] = "less_than";
    ConditionOperator["IS_TRUTHY"] = "is_truthy";
    ConditionOperator["IS_FALSY"] = "is_falsy";
    ConditionOperator["IS_EMPTY"] = "is_empty";
    ConditionOperator["IS_NOT_EMPTY"] = "is_not_empty";
    ConditionOperator["REGEX_MATCH"] = "regex_match";
})(ConditionOperator || (ConditionOperator = {}));
/**
 * Flow step execution status.
 */
export var FlowStepStatus;
(function (FlowStepStatus) {
    FlowStepStatus["PENDING"] = "pending";
    FlowStepStatus["RUNNING"] = "running";
    FlowStepStatus["SUCCESS"] = "success";
    FlowStepStatus["WARNING"] = "warning";
    FlowStepStatus["ERROR"] = "error";
    FlowStepStatus["SKIPPED"] = "skipped";
})(FlowStepStatus || (FlowStepStatus = {}));
/**
 * Action status codes returned by the browser.
 */
export var ActionStatus;
(function (ActionStatus) {
    // Success
    ActionStatus["OK"] = "ok";
    // Browser/context errors
    ActionStatus["BROWSER_NOT_FOUND"] = "browser_not_found";
    ActionStatus["BROWSER_NOT_READY"] = "browser_not_ready";
    ActionStatus["CONTEXT_NOT_FOUND"] = "context_not_found";
    // Navigation errors
    ActionStatus["NAVIGATION_FAILED"] = "navigation_failed";
    ActionStatus["NAVIGATION_TIMEOUT"] = "navigation_timeout";
    ActionStatus["PAGE_LOAD_ERROR"] = "page_load_error";
    ActionStatus["REDIRECT_DETECTED"] = "redirect_detected";
    ActionStatus["CAPTCHA_DETECTED"] = "captcha_detected";
    ActionStatus["FIREWALL_DETECTED"] = "firewall_detected";
    // Element errors
    ActionStatus["ELEMENT_NOT_FOUND"] = "element_not_found";
    ActionStatus["ELEMENT_NOT_VISIBLE"] = "element_not_visible";
    ActionStatus["ELEMENT_NOT_INTERACTABLE"] = "element_not_interactable";
    ActionStatus["ELEMENT_STALE"] = "element_stale";
    ActionStatus["MULTIPLE_ELEMENTS"] = "multiple_elements";
    // Action execution errors
    ActionStatus["CLICK_FAILED"] = "click_failed";
    ActionStatus["CLICK_INTERCEPTED"] = "click_intercepted";
    ActionStatus["TYPE_FAILED"] = "type_failed";
    ActionStatus["TYPE_PARTIAL"] = "type_partial";
    ActionStatus["SCROLL_FAILED"] = "scroll_failed";
    ActionStatus["FOCUS_FAILED"] = "focus_failed";
    ActionStatus["BLUR_FAILED"] = "blur_failed";
    ActionStatus["CLEAR_FAILED"] = "clear_failed";
    ActionStatus["PICK_FAILED"] = "pick_failed";
    ActionStatus["OPTION_NOT_FOUND"] = "option_not_found";
    ActionStatus["UPLOAD_FAILED"] = "upload_failed";
    ActionStatus["FRAME_SWITCH_FAILED"] = "frame_switch_failed";
    ActionStatus["TAB_SWITCH_FAILED"] = "tab_switch_failed";
    ActionStatus["DIALOG_NOT_HANDLED"] = "dialog_not_handled";
    // Validation errors
    ActionStatus["INVALID_SELECTOR"] = "invalid_selector";
    ActionStatus["INVALID_URL"] = "invalid_url";
    ActionStatus["INVALID_PARAMETER"] = "invalid_parameter";
    // System/timeout errors
    ActionStatus["INTERNAL_ERROR"] = "internal_error";
    ActionStatus["TIMEOUT"] = "timeout";
    ActionStatus["NETWORK_TIMEOUT"] = "network_timeout";
    ActionStatus["WAIT_TIMEOUT"] = "wait_timeout";
    ActionStatus["VERIFICATION_TIMEOUT"] = "verification_timeout";
    // Unknown
    ActionStatus["UNKNOWN"] = "unknown";
})(ActionStatus || (ActionStatus = {}));
//# sourceMappingURL=types.js.map