#pragma once

#include <string>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

class ResponseValidator {
public:
    // Response type validation
    static bool IsStringResponse(const json& response);
    static bool IsBoolResponse(const json& response);
    static bool IsJsonResponse(const json& response);
    static bool IsActionResult(const json& response);
    static bool IsErrorResponse(const json& response);

    // Get response type as string
    static std::string GetResponseType(const json& response);

    // ActionResult helpers
    static bool IsSuccess(const json& response);
    static bool HasStatus(const json& response, const std::string& status);
    static std::string GetStatus(const json& response);
    static std::string GetMessage(const json& response);

    // Get result value
    static std::string GetStringResult(const json& response);
    static bool GetBoolResult(const json& response);
    static json GetJsonResult(const json& response);

    // Validate specific response types
    static bool ValidateContextId(const json& response);
    static bool ValidateBase64Image(const json& response);
    static bool ValidateActionResultFields(const json& response);

    // All 28 valid ActionStatus codes
    static const std::vector<std::string> VALID_STATUS_CODES;

    // Check if a status code is valid
    static bool IsValidStatusCode(const std::string& status);
};
