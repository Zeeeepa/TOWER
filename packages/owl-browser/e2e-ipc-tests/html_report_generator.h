#pragma once

#include <string>
#include "json.hpp"

using json = nlohmann::json;

class HTMLReportGenerator {
public:
    // Generate HTML report from JSON data
    static std::string GenerateHTML(const json& report_data);

    // Save HTML report to file
    static bool SaveHTML(const json& report_data, const std::string& filepath);

private:
    static std::string GenerateHeader();
    static std::string GenerateStyles();
    static std::string GenerateScripts();
    static std::string GenerateSummarySection(const json& data);
    static std::string GenerateLatencySection(const json& data);
    static std::string GenerateResourceSection(const json& data);
    static std::string GenerateCategorySection(const json& data);
    static std::string GenerateMethodTable(const json& data);
    static std::string GenerateFailureSection(const json& data);
    static std::string GenerateFooter();

    static std::string EscapeHTML(const std::string& str);
};
