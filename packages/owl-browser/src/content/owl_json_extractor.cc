#include "owl_json_extractor.h"
#include "logger.h"
#include <sstream>
#include <regex>
#include <algorithm>
#include <chrono>
#include <thread>

std::string OlibJSONExtractor::EscapeJSON(const std::string& str) {
  std::string result;
  result.reserve(str.length());

  for (char c : str) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 32) {
          // Control character - escape as unicode
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          result += buf;
        } else {
          result += c;
        }
    }
  }

  return result;
}

std::string OlibJSONExtractor::BuildJSON(const std::map<std::string, std::string>& data) {
  std::ostringstream json;
  json << "{";

  bool first = true;
  for (const auto& [key, value] : data) {
    if (!first) json << ",";
    json << "\"" << EscapeJSON(key) << "\":\"" << EscapeJSON(value) << "\"";
    first = false;
  }

  json << "}";
  return json.str();
}

std::string OlibJSONExtractor::BuildJSONArray(const std::vector<std::string>& data) {
  std::ostringstream json;
  json << "[";

  bool first = true;
  for (const auto& item : data) {
    if (!first) json << ",";
    json << "\"" << EscapeJSON(item) << "\"";
    first = false;
  }

  json << "]";
  return json.str();
}

std::vector<std::string> OlibJSONExtractor::QuerySelector(
    CefRefPtr<CefFrame> frame,
    const std::string& selector,
    ExtractType extract_type,
    const std::string& attribute) {

  // Build JavaScript to execute selector
  std::ostringstream js;
  js << "(function() {\n";
  js << "  try {\n";
  js << "    const elements = document.querySelectorAll('" << selector << "');\n";
  js << "    const results = [];\n";
  js << "    for (let el of elements) {\n";

  switch (extract_type) {
    case ExtractType::TEXT:
      js << "      results.push(el.innerText || el.textContent || '');\n";
      break;
    case ExtractType::HTML:
      js << "      results.push(el.innerHTML || '');\n";
      break;
    case ExtractType::ATTRIBUTE:
      js << "      results.push(el.getAttribute('" << attribute << "') || '');\n";
      break;
    case ExtractType::VALUE:
      js << "      results.push(el.value || '');\n";
      break;
  }

  js << "    }\n";
  js << "    const el = document.getElementById('__owl_query_result') || document.createElement('div');\n";
  js << "    el.id = '__owl_query_result';\n";
  js << "    el.textContent = JSON.stringify(results);\n";
  js << "    el.style.display = 'none';\n";
  js << "    if (!el.parentNode) document.body.appendChild(el);\n";
  js << "    return results.length;\n";
  js << "  } catch(e) {\n";
  js << "    console.error('QuerySelector error:', e);\n";
  js << "    return 0;\n";
  js << "  }\n";
  js << "})();";

  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // In production, use async callback to get results
  // For now, return empty vector
  LOG_DEBUG("JSONExtractor", "Executed selector: " + selector);
  return {};
}

std::string OlibJSONExtractor::ApplyTransform(const std::string& value, Transform transform) {
  switch (transform) {
    case Transform::TRIM:
      return TransformTrim(value);
    case Transform::LOWERCASE:
      return TransformLowercase(value);
    case Transform::UPPERCASE:
      return TransformUppercase(value);
    case Transform::PARSE_INT:
      return TransformParseInt(value);
    case Transform::PARSE_FLOAT:
      return TransformParseFloat(value);
    case Transform::PARSE_PRICE:
      return TransformParsePrice(value);
    case Transform::PARSE_DATE:
      return TransformParseDate(value);
    case Transform::CLEAN_WHITESPACE:
      return TransformCleanWhitespace(value);
    case Transform::STRIP_HTML:
      return TransformStripHTML(value);
    case Transform::DECODE_ENTITIES:
      return TransformDecodeEntities(value);
    case Transform::ABSOLUTE_URL:
      return value;  // Needs base URL
    case Transform::NONE:
    default:
      return value;
  }
}

std::string OlibJSONExtractor::TransformTrim(const std::string& value) {
  size_t start = value.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  size_t end = value.find_last_not_of(" \t\n\r");
  return value.substr(start, end - start + 1);
}

std::string OlibJSONExtractor::TransformLowercase(const std::string& value) {
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

std::string OlibJSONExtractor::TransformUppercase(const std::string& value) {
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);
  return result;
}

std::string OlibJSONExtractor::TransformParseInt(const std::string& value) {
  // Extract first integer from string
  std::regex int_regex(R"([-+]?\d+)");
  std::smatch match;
  if (std::regex_search(value, match, int_regex)) {
    return match[0].str();
  }
  return "0";
}

std::string OlibJSONExtractor::TransformParseFloat(const std::string& value) {
  // Extract first float from string
  std::regex float_regex(R"([-+]?\d*\.?\d+)");
  std::smatch match;
  if (std::regex_search(value, match, float_regex)) {
    return match[0].str();
  }
  return "0.0";
}

std::string OlibJSONExtractor::TransformParsePrice(const std::string& value) {
  // Extract price: $19.99, 19.99, etc.
  std::regex price_regex(R"(\$?\s*(\d+(?:\.\d{2})?))", std::regex::icase);
  std::smatch match;
  if (std::regex_search(value, match, price_regex)) {
    return match[1].str();
  }
  return "0.00";
}

std::string OlibJSONExtractor::TransformParseDate(const std::string& value) {
  // Simplified date parsing - return as-is for now
  return TransformTrim(value);
}

std::string OlibJSONExtractor::TransformAbsoluteURL(const std::string& value, const std::string& base_url) {
  if (value.empty() || value.find("http://") == 0 || value.find("https://") == 0) {
    return value;
  }

  // Extract base domain from base_url
  std::regex base_regex(R"(^(https?://[^/]+))");
  std::smatch base_match;
  if (std::regex_search(base_url, base_match, base_regex)) {
    std::string domain = base_match[1].str();
    if (value[0] == '/') {
      return domain + value;
    } else {
      return domain + "/" + value;
    }
  }

  return value;
}

std::string OlibJSONExtractor::TransformCleanWhitespace(const std::string& value) {
  std::regex multi_space(R"(\s+)");
  std::string result = std::regex_replace(value, multi_space, " ");
  return TransformTrim(result);
}

std::string OlibJSONExtractor::TransformStripHTML(const std::string& value) {
  std::regex tag_regex("<[^>]+>");
  return std::regex_replace(value, tag_regex, "");
}

std::string OlibJSONExtractor::TransformDecodeEntities(const std::string& value) {
  std::string result = value;

  std::vector<std::pair<std::string, std::string>> entities = {
    {"&nbsp;", " "},
    {"&lt;", "<"},
    {"&gt;", ">"},
    {"&amp;", "&"},
    {"&quot;", "\""},
    {"&apos;", "'"},
  };

  for (const auto& [entity, replacement] : entities) {
    size_t pos = 0;
    while ((pos = result.find(entity, pos)) != std::string::npos) {
      result.replace(pos, entity.length(), replacement);
      pos += replacement.length();
    }
  }

  return result;
}

std::string OlibJSONExtractor::ExtractField(
    CefRefPtr<CefFrame> frame,
    const FieldRule& rule) {

  // Query selector and extract
  std::vector<std::string> results = QuerySelector(frame, rule.selector,
                                                   rule.extract, rule.attribute);

  if (results.empty()) {
    return rule.multiple ? "[]" : "\"\"";
  }

  // Apply transform
  for (auto& result : results) {
    result = ApplyTransform(result, rule.transform);
  }

  if (rule.multiple) {
    return BuildJSONArray(results);
  } else {
    return "\"" + EscapeJSON(results[0]) + "\"";
  }
}

std::string OlibJSONExtractor::ExtractWithTemplate(
    CefRefPtr<CefFrame> frame,
    const ExtractionTemplate& templ) {

  LOG_DEBUG("JSONExtractor", "Extracting with template: " + templ.name);

  std::ostringstream json;
  json << "{\n";
  json << "  \"template\": \"" << templ.name << "\",\n";
  json << "  \"version\": \"" << templ.version << "\",\n";
  json << "  \"data\": {\n";

  bool first = true;
  for (const auto& [field_name, rule] : templ.extraction) {
    if (!first) json << ",\n";

    std::string value = ExtractField(frame, rule);
    json << "    \"" << field_name << "\": " << value;

    first = false;
  }

  json << "\n  }\n";
  json << "}";

  return json.str();
}

std::string OlibJSONExtractor::ExtractWithSchema(
    CefRefPtr<CefFrame> frame,
    const std::map<std::string, FieldRule>& schema) {

  LOG_DEBUG("JSONExtractor", "Extracting with custom schema");

  std::ostringstream json;
  json << "{\n";

  bool first = true;
  for (const auto& [field_name, rule] : schema) {
    if (!first) json << ",\n";

    std::string value = ExtractField(frame, rule);
    json << "  \"" << field_name << "\": " << value;

    first = false;
  }

  json << "\n}";

  return json.str();
}

std::string OlibJSONExtractor::ExtractGeneric(CefRefPtr<CefFrame> frame) {
  LOG_DEBUG("JSONExtractor", "Extracting generic page structure");

  // For now, return a simplified JSON structure
  // Full implementation requires working async JS execution
  std::ostringstream json;
  json << "{\n";
  json << "  \"url\": \"" + EscapeJSON(frame->GetURL().ToString()) + "\",\n";
  json << "  \"title\": \"Page Title\",\n";
  json << "  \"note\": \"Full JSON extraction requires async JS callback implementation\"\n";
  json << "}";

  LOG_DEBUG("JSONExtractor", "Generic extraction complete");
  return json.str();
}
