#include "response_validator.h"

// All 28 valid ActionStatus codes from action_result.h
const std::vector<std::string> ResponseValidator::VALID_STATUS_CODES = {
    // Success
    "ok",
    // Browser/context errors
    "browser_not_found",
    "browser_not_ready",
    "context_not_found",
    // Navigation errors
    "navigation_failed",
    "navigation_timeout",
    "page_load_error",
    "redirect_detected",
    "captcha_detected",
    "firewall_detected",
    // Element interaction errors
    "element_not_found",
    "element_not_visible",
    "element_not_interactable",
    "element_stale",
    "multiple_elements",
    // Action execution errors
    "click_failed",
    "type_failed",
    "scroll_failed",
    "focus_failed",
    "blur_failed",
    "clear_failed",
    // Validation errors
    "invalid_selector",
    "invalid_url",
    "invalid_parameter",
    // System errors
    "internal_error",
    "timeout",
    "network_timeout",
    "wait_timeout",
    // Unknown
    "unknown"
};

bool ResponseValidator::IsStringResponse(const json& response) {
    if (!response.contains("result")) return false;
    return response["result"].is_string();
}

bool ResponseValidator::IsBoolResponse(const json& response) {
    if (!response.contains("result")) return false;
    return response["result"].is_boolean();
}

bool ResponseValidator::IsJsonResponse(const json& response) {
    if (!response.contains("result")) return false;
    const auto& result = response["result"];
    // Raw JSON response is an object or array that is NOT an ActionResult
    if (result.is_object()) {
        // ActionResult has "success" and "status" fields
        return !result.contains("success") || !result.contains("status");
    }
    return result.is_array();
}

bool ResponseValidator::IsActionResult(const json& response) {
    if (!response.contains("result")) return false;
    const auto& result = response["result"];
    if (!result.is_object()) return false;
    // ActionResult has "success", "status", and "message" fields
    return result.contains("success") && result.contains("status") && result.contains("message");
}

bool ResponseValidator::IsErrorResponse(const json& response) {
    return response.contains("error");
}

std::string ResponseValidator::GetResponseType(const json& response) {
    if (IsErrorResponse(response)) return "Error";
    if (IsActionResult(response)) return "ActionResult";
    if (IsBoolResponse(response)) return "Boolean";
    if (IsStringResponse(response)) return "String";
    if (IsJsonResponse(response)) return "JSON";
    return "Unknown";
}

bool ResponseValidator::IsSuccess(const json& response) {
    if (IsErrorResponse(response)) return false;

    if (IsActionResult(response)) {
        return response["result"]["success"].get<bool>();
    }

    if (IsBoolResponse(response)) {
        return response["result"].get<bool>();
    }

    // String and JSON responses are considered success if no error
    return response.contains("result");
}

bool ResponseValidator::HasStatus(const json& response, const std::string& status) {
    if (!IsActionResult(response)) return false;
    return response["result"]["status"].get<std::string>() == status;
}

std::string ResponseValidator::GetStatus(const json& response) {
    if (IsActionResult(response)) {
        return response["result"]["status"].get<std::string>();
    }
    if (IsErrorResponse(response)) {
        return "error";
    }
    if (IsSuccess(response)) {
        return "ok";
    }
    return "unknown";
}

std::string ResponseValidator::GetMessage(const json& response) {
    if (IsActionResult(response) && response["result"].contains("message")) {
        return response["result"]["message"].get<std::string>();
    }
    if (IsErrorResponse(response)) {
        return response["error"].get<std::string>();
    }
    return "";
}

std::string ResponseValidator::GetStringResult(const json& response) {
    if (!response.contains("result")) return "";
    if (response["result"].is_string()) {
        return response["result"].get<std::string>();
    }
    // Handle createContext response which returns object with context_id
    if (response["result"].is_object() && response["result"].contains("context_id")) {
        return response["result"]["context_id"].get<std::string>();
    }
    return "";
}

bool ResponseValidator::GetBoolResult(const json& response) {
    if (!response.contains("result")) return false;
    if (response["result"].is_boolean()) {
        return response["result"].get<bool>();
    }
    return false;
}

json ResponseValidator::GetJsonResult(const json& response) {
    if (!response.contains("result")) return json::object();
    return response["result"];
}

bool ResponseValidator::ValidateContextId(const json& response) {
    if (!response.contains("result")) return false;

    std::string ctx;
    // Handle old format (string) and new format (object with context_id)
    if (response["result"].is_string()) {
        ctx = response["result"].get<std::string>();
    } else if (response["result"].is_object() && response["result"].contains("context_id")) {
        ctx = response["result"]["context_id"].get<std::string>();
    } else {
        return false;
    }

    // Context IDs start with "ctx_" followed by digits
    return ctx.substr(0, 4) == "ctx_" && ctx.length() > 4;
}

bool ResponseValidator::ValidateBase64Image(const json& response) {
    if (!IsStringResponse(response)) return false;
    std::string data = response["result"].get<std::string>();
    // PNG starts with specific base64 sequence
    return data.length() > 100 && data.substr(0, 4) == "iVBO";  // PNG header in base64
}

bool ResponseValidator::ValidateActionResultFields(const json& response) {
    if (!IsActionResult(response)) return false;
    const auto& result = response["result"];

    // Required fields
    if (!result.contains("success") || !result["success"].is_boolean()) return false;
    if (!result.contains("status") || !result["status"].is_string()) return false;
    if (!result.contains("message") || !result["message"].is_string()) return false;

    // Optional fields should have correct types if present
    if (result.contains("selector") && !result["selector"].is_string()) return false;
    if (result.contains("url") && !result["url"].is_string()) return false;
    if (result.contains("error_code") && !result["error_code"].is_string()) return false;
    if (result.contains("http_status") && !result["http_status"].is_number_integer()) return false;
    if (result.contains("element_count") && !result["element_count"].is_number_integer()) return false;

    return true;
}

bool ResponseValidator::IsValidStatusCode(const std::string& status) {
    for (const auto& code : VALID_STATUS_CODES) {
        if (code == status) return true;
    }
    return false;
}
