#pragma once

#include <string>
#include <vector>
#include <map>
#include "include/cef_frame.h"
#include "owl_extraction_template.h"

// JSON extraction engine
// Execute template rules and build structured JSON
// Built BY AI FOR AI

class OlibJSONExtractor {
public:
  // Extract JSON using template
  static std::string ExtractWithTemplate(
      CefRefPtr<CefFrame> frame,
      const ExtractionTemplate& templ);

  // Extract JSON using custom schema
  static std::string ExtractWithSchema(
      CefRefPtr<CefFrame> frame,
      const std::map<std::string, FieldRule>& schema);

  // Extract generic page structure
  static std::string ExtractGeneric(CefRefPtr<CefFrame> frame);

private:
  // Execute field extraction rule
  static std::string ExtractField(
      CefRefPtr<CefFrame> frame,
      const FieldRule& rule);

  // Execute selector and get element(s)
  static std::vector<std::string> QuerySelector(
      CefRefPtr<CefFrame> frame,
      const std::string& selector,
      ExtractType extract_type,
      const std::string& attribute);

  // Apply transformation
  static std::string ApplyTransform(const std::string& value, Transform transform);

  // Helpers
  static std::string BuildJSON(const std::map<std::string, std::string>& data);
  static std::string BuildJSONArray(const std::vector<std::string>& data);
  static std::string EscapeJSON(const std::string& str);

  // Transform implementations
  static std::string TransformTrim(const std::string& value);
  static std::string TransformLowercase(const std::string& value);
  static std::string TransformUppercase(const std::string& value);
  static std::string TransformParseInt(const std::string& value);
  static std::string TransformParseFloat(const std::string& value);
  static std::string TransformParsePrice(const std::string& value);
  static std::string TransformParseDate(const std::string& value);
  static std::string TransformAbsoluteURL(const std::string& value, const std::string& base_url);
  static std::string TransformCleanWhitespace(const std::string& value);
  static std::string TransformStripHTML(const std::string& value);
  static std::string TransformDecodeEntities(const std::string& value);
};
