#include "owl_test_scheme_handler.h"
#include "owl_browser_manager.h"
#include "owl_platform_utils.h"
#include "owl_spoof_manager.h"
#include "owl_stealth.h"
#include "owl_virtual_machine.h"
#include "stealth/workers/owl_worker_patcher.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <regex>

// Get the path to the app bundle's Resources folder (cross-platform)
std::string GetResourcesPath() {
  LOG_DEBUG("TestScheme", "GetResourcesPath CALLED");

  std::string resources_path = OlibPlatform::GetResourcesDir();
  if (!resources_path.empty()) {
    LOG_DEBUG("TestScheme", "Resources path: " + resources_path);
    return resources_path;
  }

  // Fallback to current directory
  LOG_DEBUG("TestScheme", "Using fallback: '.'");
  return ".";
}

OwlTestSchemeHandler::OwlTestSchemeHandler()
    : offset_(0), status_code_(200) {
  LOG_DEBUG("TestScheme", "Constructor called - Handler instance created");
}

std::string OwlTestSchemeHandler::MapTestUrlToFilePath(const std::string& url) {
  LOG_DEBUG("TestScheme", "MapTestUrlToFilePath CALLED with URL: " + url);

  // Parse URL:
  // owl://signin_form.html -> statics/signin_form/index.html
  // owl://signin_form.html/css/styles.css -> statics/signin_form/css/styles.css

  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    LOG_ERROR("TestScheme", "No '://' found in URL: " + url);
    return "";
  }

  std::string path = url.substr(scheme_end + 3);  // Skip "://"
  LOG_DEBUG("TestScheme", "After scheme: '" + path + "'");

  // Remove trailing slash if present (CEF adds this)
  if (!path.empty() && path[path.length() - 1] == '/') {
    path = path.substr(0, path.length() - 1);
  }

  // Remove query string if present
  size_t query_pos = path.find('?');
  if (query_pos != std::string::npos) {
    path = path.substr(0, query_pos);
  }

  // Remove fragment if present
  size_t fragment_pos = path.find('#');
  if (fragment_pos != std::string::npos) {
    path = path.substr(0, fragment_pos);
  }

  if (path.empty()) {
    LOG_ERROR("TestScheme", "Empty path after parsing");
    return "";
  }

  // Safety: Check for directory traversal in the URL path
  if (path.find("..") != std::string::npos) {
    LOG_ERROR("TestScheme", "Directory traversal detected in URL path: " + path);
    return "";
  }

  LOG_DEBUG("TestScheme", "Cleaned path: '" + path + "'");

  // Now we have paths like:
  // "signin_form.html" -> statics/signin_form/index.html
  // "signin_form.html/css/styles.css" -> statics/signin_form/css/styles.css
  // "logo.png" -> logo.png (direct resource)

  std::string final_path;

  // Check if path contains .html (form-based resources)
  size_t html_pos = path.find(".html");
  if (html_pos != std::string::npos) {
    // Path contains .html - it's either the HTML itself or a resource from it
    std::string form_name = path.substr(0, html_pos);
    LOG_DEBUG("TestScheme", "Form name: '" + form_name + "'");

    // Check if there's a resource path after .html
    size_t resource_start = html_pos + 5; // ".html" is 5 chars
    if (resource_start < path.length() && path[resource_start] == '/') {
      // Resource request: signin_form.html/images/captcha/37UY6.png
      std::string resource_path = path.substr(resource_start + 1); // Skip the /
      LOG_DEBUG("TestScheme", "Resource path: '" + resource_path + "'");
      final_path = "statics/" + form_name + "/" + resource_path;
    } else {
      // Main HTML request: signin_form.html
      LOG_DEBUG("TestScheme", "Main HTML request");
      final_path = "statics/" + form_name + "/index.html";
    }
  }
  // Check if it's a direct resource file (images, videos, etc.) - AFTER checking for .html
  else if (path.find(".png") != std::string::npos ||
           path.find(".apng") != std::string::npos ||
           path.find(".jpg") != std::string::npos ||
           path.find(".jpeg") != std::string::npos ||
           path.find(".svg") != std::string::npos ||
           path.find(".gif") != std::string::npos ||
           path.find(".ico") != std::string::npos ||
           path.find(".webp") != std::string::npos ||
           path.find(".mp4") != std::string::npos ||
           path.find(".webm") != std::string::npos ||
           path.find(".ogg") != std::string::npos) {
    // Direct resource file (no .html in path)
    LOG_DEBUG("TestScheme", "Direct resource file detected");
    // Handle resources/ prefix - strip it to get the actual filename
    if (path.find("resources/") == 0) {
      final_path = path.substr(10);  // Skip "resources/"
      LOG_DEBUG("TestScheme", "Stripped resources/ prefix, path: '" + final_path + "'");
    } else {
      final_path = path;
    }
  }
  else {
    LOG_ERROR("TestScheme", "No .html or direct resource found in path: " + path);
    return "";
  }

  LOG_DEBUG("TestScheme", "FINAL MAPPED PATH: '" + final_path + "'");
  return final_path;
}

std::string OwlTestSchemeHandler::GetMimeType(const std::string& extension) {
  if (extension == ".html" || extension == ".htm") return "text/html";
  if (extension == ".css") return "text/css";
  if (extension == ".js") return "application/javascript";
  if (extension == ".json") return "application/json";
  if (extension == ".png") return "image/png";
  if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
  if (extension == ".gif") return "image/gif";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".webp") return "image/webp";
  if (extension == ".ico") return "image/x-icon";
  if (extension == ".woff") return "font/woff";
  if (extension == ".woff2") return "font/woff2";
  if (extension == ".ttf") return "font/ttf";
  if (extension == ".otf") return "font/otf";
  if (extension == ".xml") return "application/xml";
  if (extension == ".pdf") return "application/pdf";
  if (extension == ".txt") return "text/plain";
  if (extension == ".mp4") return "video/mp4";
  if (extension == ".webm") return "video/webm";
  if (extension == ".ogg") return "video/ogg";

  return "application/octet-stream";
}

bool OwlTestSchemeHandler::LoadFile(const std::string& file_path) {
  LOG_DEBUG("TestScheme", "LoadFile CALLED with path: '" + file_path + "'");

  // Safety: Validate file path is not empty
  if (file_path.empty()) {
    LOG_ERROR("TestScheme", "Empty file path");
    status_code_ = 400;
    return false;
  }

  // Note: Directory traversal check is done in MapTestUrlToFilePath
  // before the path reaches here, so we don't check the full path again
  // (full path contains legitimate ../ for app bundle Resources folder)

  std::ifstream file(file_path, std::ios::binary);

  if (!file.is_open()) {
    LOG_ERROR("TestScheme", "Failed to open file: " + file_path);
    status_code_ = 404;

    // Generate 404 HTML (without exposing file path)
    std::string html =
      "<!DOCTYPE html>"
      "<html><head><title>404 Not Found</title></head>"
      "<body><h1>404 Not Found</h1>"
      "<p>The requested resource was not found</p>"
      "</body></html>";

    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    return false;
  }

  LOG_DEBUG("TestScheme", "File opened successfully, reading content...");

  // Read entire file into memory
  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(0, std::ios::beg);

  LOG_DEBUG("TestScheme", "File size: " + std::to_string(size) + " bytes");

  // Safety: Prevent loading excessively large files (10MB limit)
  const size_t MAX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
  if (size > MAX_FILE_SIZE) {
    LOG_ERROR("TestScheme", "File too large: " + std::to_string(size) +
              " bytes (max: " + std::to_string(MAX_FILE_SIZE) + ")");
    file.close();
    std::string html =
      "<!DOCTYPE html>"
      "<html><head><title>413 Payload Too Large</title></head>"
      "<body><h1>413 Payload Too Large</h1>"
      "<p>File is too large to load</p>"
      "</body></html>";
    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    status_code_ = 413;
    return false;
  }

  // Safety: Resize data buffer
  // Note: Without exceptions, resize may fail silently on OOM
  // Check capacity after resize to detect allocation failures
  data_.resize(size);

  file.read(reinterpret_cast<char*>(data_.data()), size);

  // Safety: Check if read was successful
  if (!file) {
    LOG_ERROR("TestScheme", "Failed to read file completely");
    file.close();
    status_code_ = 500;
    return false;
  }

  file.close();

  LOG_DEBUG("TestScheme", "File read complete, " + std::to_string(data_.size()) + " bytes loaded");

  // Determine MIME type from file extension
  size_t dot_pos = file_path.rfind('.');
  if (dot_pos != std::string::npos) {
    std::string ext = file_path.substr(dot_pos);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    mime_type_ = GetMimeType(ext);
    LOG_DEBUG("TestScheme", "File extension: '" + ext + "' -> MIME type: " + mime_type_);
  } else {
    mime_type_ = "application/octet-stream";
    LOG_DEBUG("TestScheme", "No extension found, using default MIME type: " + mime_type_);
  }

  status_code_ = 200;
  LOG_DEBUG("TestScheme", "SUCCESS! Loaded file: " + file_path + " (" +
           std::to_string(size) + " bytes, MIME: " + mime_type_ + ")");

  return true;
}

bool OwlTestSchemeHandler::Open(CefRefPtr<CefRequest> request,
                                 bool& handle_request,
                                 CefRefPtr<CefCallback> callback) {
  std::string url = request->GetURL().ToString();
  LOG_DEBUG("TestScheme", "========================================");
  LOG_DEBUG("TestScheme", "Open() CALLED! Request URL: " + url);

  // Special case: homepage.html serves C++ generated HTML with demographics
  if (url.find("homepage.html") != std::string::npos) {
    LOG_DEBUG("TestScheme", "Serving dynamically generated homepage");
    std::string html = OwlBrowserManager::GetInstance()->GetHomepageHTML();
    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    status_code_ = 200;
    offset_ = 0;
    handle_request = true;
    return true;
  }

  // Special case: playground.html serves C++ generated playground UI
  if (url.find("playground.html") != std::string::npos) {
    LOG_DEBUG("TestScheme", "Serving dynamically generated playground");
    std::string html = OwlBrowserManager::GetInstance()->GetPlaygroundHTML();
    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    status_code_ = 200;
    offset_ = 0;
    handle_request = true;
    return true;
  }

  // Special case: devconsole.html serves C++ generated developer console UI
  if (url.find("devconsole.html") != std::string::npos) {
    LOG_DEBUG("TestScheme", "Serving dynamically generated developer console");
    std::string html = OwlBrowserManager::GetInstance()->GetDevConsoleHTML();
    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    status_code_ = 200;
    offset_ = 0;
    handle_request = true;
    return true;
  }

  // Map URL to file path
  std::string file_path = MapTestUrlToFilePath(url);
  LOG_DEBUG("TestScheme", "Mapped file path: '" + file_path + "'");

  if (file_path.empty()) {
    LOG_ERROR("TestScheme", "Invalid test URL (empty file_path): " + url);
    status_code_ = 400;
    std::string html =
      "<!DOCTYPE html>"
      "<html><head><title>400 Bad Request</title></head>"
      "<body><h1>400 Bad Request</h1>"
      "<p>Invalid test URL format</p>"
      "</body></html>";
    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    handle_request = true;
    LOG_DEBUG("TestScheme", "Returning 400 error page");
    return true;
  }

  // Get the Resources folder path and construct full path
  std::string resources_path = GetResourcesPath();
  LOG_DEBUG("TestScheme", "Resources path: '" + resources_path + "'");

  std::string full_path = resources_path + "/" + file_path;
  LOG_DEBUG("TestScheme", "FULL PATH TO LOAD: '" + full_path + "'");

  // Load the file
#ifdef OWL_DEBUG_BUILD
  bool load_success = LoadFile(full_path);
  LOG_DEBUG("TestScheme", "LoadFile result: " + std::string(load_success ? "SUCCESS" : "FAILED"));
  LOG_DEBUG("TestScheme", "Status code: " + std::to_string(status_code_));
  LOG_DEBUG("TestScheme", "Data size: " + std::to_string(data_.size()) + " bytes");
  LOG_DEBUG("TestScheme", "MIME type: " + mime_type_);
#else
  LoadFile(full_path);
#endif

  handle_request = true;
  LOG_DEBUG("TestScheme", "Open() returning TRUE, handle_request=TRUE");
  LOG_DEBUG("TestScheme", "========================================");
  return true;
}

void OwlTestSchemeHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                               int64_t& response_length,
                                               CefString& redirectUrl) {
  LOG_DEBUG("TestScheme", "GetResponseHeaders CALLED!");
  LOG_DEBUG("TestScheme", "Setting status code: " + std::to_string(status_code_));
  LOG_DEBUG("TestScheme", "Setting MIME type: " + mime_type_);
  LOG_DEBUG("TestScheme", "Response length: " + std::to_string(data_.size()) + " bytes");

  response->SetStatus(status_code_);
  response->SetMimeType(mime_type_);
  response_length = data_.size();

  // Set headers
  CefResponse::HeaderMap headers;
  headers.insert(std::make_pair("Cache-Control", "no-cache"));
  headers.insert(std::make_pair("Access-Control-Allow-Origin", "*"));
  headers.insert(std::make_pair("Accept-Ranges", "bytes"));
  response->SetHeaderMap(headers);

  LOG_DEBUG("TestScheme", "Headers set, returning");
}

bool OwlTestSchemeHandler::Read(void* data_out,
                                 int bytes_to_read,
                                 int& bytes_read,
                                 CefRefPtr<CefResourceReadCallback> callback) {
  LOG_DEBUG("TestScheme", "Read CALLED!");
  LOG_DEBUG("TestScheme", "Offset: " + std::to_string(offset_) +
            ", Data size: " + std::to_string(data_.size()) +
            ", Bytes to read: " + std::to_string(bytes_to_read));

  bool has_data = false;
  bytes_read = 0;

  if (offset_ < data_.size()) {
    // Copy data to output buffer
    int transfer_size = std::min(static_cast<int>(data_.size() - offset_),
                                 bytes_to_read);
    memcpy(data_out, data_.data() + offset_, transfer_size);
    offset_ += transfer_size;
    bytes_read = transfer_size;
    has_data = true;
    LOG_DEBUG("TestScheme", "Transferred " + std::to_string(transfer_size) +
              " bytes, new offset: " + std::to_string(offset_));
  } else {
    LOG_DEBUG("TestScheme", "No more data to read (EOF)");
  }

  LOG_DEBUG("TestScheme", "Read returning has_data=" + std::string(has_data ? "TRUE" : "FALSE"));
  return has_data;
}

void OwlTestSchemeHandler::Cancel() {
  // Nothing to cancel
}

// Factory implementation
OwlTestSchemeHandlerFactory::OwlTestSchemeHandlerFactory() {
  LOG_DEBUG("TestSchemeFactory", "Factory constructor called - Factory instance created!");
}

CefRefPtr<CefResourceHandler> OwlTestSchemeHandlerFactory::Create(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& scheme_name,
    CefRefPtr<CefRequest> request) {

  std::string url = request->GetURL().ToString();
  std::string scheme = scheme_name.ToString();

  LOG_DEBUG("TestSchemeFactory", "######################################");
  LOG_DEBUG("TestSchemeFactory", "### FACTORY CREATE() CALLED! ###");
  LOG_DEBUG("TestSchemeFactory", "URL: " + url);
  LOG_DEBUG("TestSchemeFactory", "Scheme name: " + scheme);
  LOG_DEBUG("TestSchemeFactory", "Browser: " + std::string(browser ? "YES" : "NO"));
  LOG_DEBUG("TestSchemeFactory", "Frame: " + std::string(frame ? "YES" : "NO"));
  if (frame) {
    LOG_DEBUG("TestSchemeFactory", "Frame Name: " + frame->GetName().ToString());
  }
  LOG_DEBUG("TestSchemeFactory", "Creating new OwlTestSchemeHandler...");

  CefRefPtr<CefResourceHandler> handler = new OwlTestSchemeHandler();

  LOG_DEBUG("TestSchemeFactory", "Handler created, returning");
  LOG_DEBUG("TestSchemeFactory", "######################################");

  return handler;
}

// ============================================================================
// OwlHttpsResourceHandler - Handles HTTPS requests to *.owl domains
// Maps URLs like https://lie-detector.owl/ to statics/lie_detector/index.html
// ============================================================================

OwlHttpsResourceHandler::OwlHttpsResourceHandler(const std::string& domain, const std::string& url, int browser_id)
    : domain_(domain), url_(url), offset_(0), status_code_(200), browser_id_(browser_id) {
  LOG_DEBUG("HttpsHandler", "Constructor - domain: " + domain + ", url: " + url + ", browser_id: " + std::to_string(browser_id));
}

std::string OwlHttpsResourceHandler::MapHttpsUrlToFilePath(const std::string& url) {
  LOG_DEBUG("HttpsHandler", "MapHttpsUrlToFilePath CALLED with URL: " + url);

  // Parse URL: https://lie-detector.owl/path/to/resource
  // Output: statics/lie_detector/path/to/resource

  // Extract domain from URL
  size_t protocol_end = url.find("://");
  if (protocol_end == std::string::npos) {
    LOG_ERROR("HttpsHandler", "No '://' found in URL: " + url);
    return "";
  }
  protocol_end += 3;  // Skip "://"

  // Find end of domain (start of path)
  size_t domain_end = url.find('/', protocol_end);
  if (domain_end == std::string::npos) {
    domain_end = url.length();
  }

  std::string domain_with_port = url.substr(protocol_end, domain_end - protocol_end);
  LOG_DEBUG("HttpsHandler", "Domain (with port): " + domain_with_port);

  // Strip port number if present (e.g., "lie-detector.owl:8443" -> "lie-detector.owl")
  size_t port_pos = domain_with_port.find(':');
  std::string domain = (port_pos != std::string::npos)
      ? domain_with_port.substr(0, port_pos)
      : domain_with_port;
  LOG_DEBUG("HttpsHandler", "Domain (without port): " + domain);

  // Verify it's a .owl domain
  if (domain.length() <= 4 || domain.substr(domain.length() - 4) != ".owl") {
    LOG_ERROR("HttpsHandler", "Not a .owl domain: " + domain);
    return "";
  }

  // Extract the page name (domain without .owl)
  // e.g., "lie-detector.owl" -> "lie-detector"
  std::string page_name = domain.substr(0, domain.length() - 4);
  LOG_DEBUG("HttpsHandler", "Page name (before underscore conversion): " + page_name);

  // Convert hyphens to underscores (lie-detector -> lie_detector)
  std::replace(page_name.begin(), page_name.end(), '-', '_');
  LOG_DEBUG("HttpsHandler", "Page name (after underscore conversion): " + page_name);

  // Extract path after domain
  std::string path = "";
  if (domain_end < url.length()) {
    path = url.substr(domain_end + 1);  // Skip the '/'
  }

  // Remove query string and fragment
  size_t query_pos = path.find('?');
  if (query_pos != std::string::npos) {
    path = path.substr(0, query_pos);
  }
  size_t fragment_pos = path.find('#');
  if (fragment_pos != std::string::npos) {
    path = path.substr(0, fragment_pos);
  }

  // Remove trailing slash
  if (!path.empty() && path[path.length() - 1] == '/') {
    path = path.substr(0, path.length() - 1);
  }

  LOG_DEBUG("HttpsHandler", "Path: '" + path + "'");

  // Safety: Check for directory traversal
  if (path.find("..") != std::string::npos) {
    LOG_ERROR("HttpsHandler", "Directory traversal detected in path: " + path);
    return "";
  }

  // Map to file path
  std::string final_path;
  if (path.empty()) {
    // Root request: https://lie-detector.owl/ -> statics/lie_detector/index.html
    final_path = "statics/" + page_name + "/index.html";
  } else {
    // Resource request: https://lie-detector.owl/css/styles.css -> statics/lie_detector/css/styles.css
    final_path = "statics/" + page_name + "/" + path;
  }

  LOG_DEBUG("HttpsHandler", "FINAL MAPPED PATH: '" + final_path + "'");
  return final_path;
}

bool OwlHttpsResourceHandler::LoadFile(const std::string& file_path) {
  LOG_DEBUG("HttpsHandler", "LoadFile CALLED with path: '" + file_path + "'");

  if (file_path.empty()) {
    LOG_ERROR("HttpsHandler", "Empty file path");
    status_code_ = 400;
    return false;
  }

  std::ifstream file(file_path, std::ios::binary);

  if (!file.is_open()) {
    LOG_ERROR("HttpsHandler", "Failed to open file: " + file_path);
    status_code_ = 404;

    std::string html =
      "<!DOCTYPE html>"
      "<html><head><title>404 Not Found</title></head>"
      "<body><h1>404 Not Found</h1>"
      "<p>The requested resource was not found</p>"
      "</body></html>";

    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    return false;
  }

  LOG_DEBUG("HttpsHandler", "File opened successfully, reading content...");

  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(0, std::ios::beg);

  LOG_DEBUG("HttpsHandler", "File size: " + std::to_string(size) + " bytes");

  // Limit file size to 10MB
  const size_t MAX_FILE_SIZE = 10 * 1024 * 1024;
  if (size > MAX_FILE_SIZE) {
    LOG_ERROR("HttpsHandler", "File too large: " + std::to_string(size));
    file.close();
    status_code_ = 413;
    return false;
  }

  data_.resize(size);
  file.read(reinterpret_cast<char*>(data_.data()), size);

  if (!file) {
    LOG_ERROR("HttpsHandler", "Failed to read file completely");
    file.close();
    status_code_ = 500;
    return false;
  }

  file.close();

  // Determine MIME type from extension
  size_t dot_pos = file_path.rfind('.');
  if (dot_pos != std::string::npos) {
    std::string ext = file_path.substr(dot_pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Use same MIME type lookup as OwlTestSchemeHandler
    if (ext == ".html" || ext == ".htm") mime_type_ = "text/html";
    else if (ext == ".css") mime_type_ = "text/css";
    else if (ext == ".js") mime_type_ = "application/javascript";
    else if (ext == ".json") mime_type_ = "application/json";
    else if (ext == ".png") mime_type_ = "image/png";
    else if (ext == ".jpg" || ext == ".jpeg") mime_type_ = "image/jpeg";
    else if (ext == ".gif") mime_type_ = "image/gif";
    else if (ext == ".svg") mime_type_ = "image/svg+xml";
    else if (ext == ".webp") mime_type_ = "image/webp";
    else if (ext == ".ico") mime_type_ = "image/x-icon";
    else if (ext == ".woff") mime_type_ = "font/woff";
    else if (ext == ".woff2") mime_type_ = "font/woff2";
    else if (ext == ".ttf") mime_type_ = "font/ttf";
    else if (ext == ".otf") mime_type_ = "font/otf";
    else if (ext == ".mp4") mime_type_ = "video/mp4";
    else if (ext == ".webm") mime_type_ = "video/webm";
    else mime_type_ = "application/octet-stream";

    LOG_DEBUG("HttpsHandler", "File extension: '" + ext + "' -> MIME type: " + mime_type_);
  } else {
    mime_type_ = "application/octet-stream";
  }

  status_code_ = 200;
  LOG_DEBUG("HttpsHandler", "SUCCESS! Loaded file: " + file_path + " (" +
           std::to_string(size) + " bytes, MIME: " + mime_type_ + ")");

  return true;
}

bool OwlHttpsResourceHandler::Open(CefRefPtr<CefRequest> request,
                                   bool& handle_request,
                                   CefRefPtr<CefCallback> callback) {
  std::string url = request->GetURL().ToString();
  LOG_DEBUG("HttpsHandler", "========================================");
  LOG_DEBUG("HttpsHandler", "Open() CALLED! Request URL: " + url);

  // Map URL to file path
  std::string file_path = MapHttpsUrlToFilePath(url);
  LOG_DEBUG("HttpsHandler", "Mapped file path: '" + file_path + "'");

  if (file_path.empty()) {
    LOG_ERROR("HttpsHandler", "Invalid .owl URL (empty file_path): " + url);
    status_code_ = 400;
    std::string html =
      "<!DOCTYPE html>"
      "<html><head><title>400 Bad Request</title></head>"
      "<body><h1>400 Bad Request</h1>"
      "<p>Invalid .owl URL format</p>"
      "</body></html>";
    data_.assign(html.begin(), html.end());
    mime_type_ = "text/html";
    handle_request = true;
    return true;
  }

  // Get Resources folder path and construct full path
  std::string resources_path = GetResourcesPath();
  std::string full_path = resources_path + "/" + file_path;
  LOG_DEBUG("HttpsHandler", "FULL PATH TO LOAD: '" + full_path + "'");

  // Load the file
  LoadFile(full_path);

  // CRITICAL: Prepend worker patches for ServiceWorker/Worker scripts
  // This fixes the timing bug where blob workers created inside SW have native values
  PrependWorkerPatches();

  handle_request = true;
  LOG_DEBUG("HttpsHandler", "Open() returning TRUE");
  LOG_DEBUG("HttpsHandler", "========================================");
  return true;
}

void OwlHttpsResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                                  int64_t& response_length,
                                                  CefString& redirectUrl) {
  LOG_DEBUG("HttpsHandler", "GetResponseHeaders CALLED!");
  LOG_DEBUG("HttpsHandler", "Setting status code: " + std::to_string(status_code_));
  LOG_DEBUG("HttpsHandler", "Setting MIME type: " + mime_type_);
  LOG_DEBUG("HttpsHandler", "Response length: " + std::to_string(data_.size()) + " bytes");

  response->SetStatus(status_code_);
  response->SetMimeType(mime_type_);
  response_length = data_.size();

  // Set headers for secure context
  CefResponse::HeaderMap headers;
  headers.insert(std::make_pair("Cache-Control", "no-cache"));
  headers.insert(std::make_pair("Access-Control-Allow-Origin", "*"));
  headers.insert(std::make_pair("Accept-Ranges", "bytes"));
  // ServiceWorker requires these headers
  headers.insert(std::make_pair("X-Content-Type-Options", "nosniff"));
  response->SetHeaderMap(headers);

  LOG_DEBUG("HttpsHandler", "Headers set, returning");
}

bool OwlHttpsResourceHandler::Read(void* data_out,
                                   int bytes_to_read,
                                   int& bytes_read,
                                   CefRefPtr<CefResourceReadCallback> callback) {
  LOG_DEBUG("HttpsHandler", "Read CALLED!");

  bool has_data = false;
  bytes_read = 0;

  if (offset_ < data_.size()) {
    int transfer_size = std::min(static_cast<int>(data_.size() - offset_),
                                 bytes_to_read);
    memcpy(data_out, data_.data() + offset_, transfer_size);
    offset_ += transfer_size;
    bytes_read = transfer_size;
    has_data = true;
    LOG_DEBUG("HttpsHandler", "Transferred " + std::to_string(transfer_size) + " bytes");
  }

  return has_data;
}

void OwlHttpsResourceHandler::Cancel() {
  // Nothing to cancel
}

// ============================================================================
// PrependWorkerPatches - Prepend spoofing patches to worker/SW scripts
// This is CRITICAL for ServiceWorker spoofing because CEF's ResponseFilter
// does not apply to custom CefResourceHandler implementations.
//
// Uses the unified WorkerPatcher classes from stealth/workers/
// ============================================================================
void OwlHttpsResourceHandler::PrependWorkerPatches() {
  // Check if this is a JavaScript file that needs patching
  if (mime_type_ != "application/javascript") {
    return;
  }

  // Check if URL matches worker script patterns
  if (!owl::workers::WorkerPatcher::IsWorkerScript(url_)) {
    LOG_DEBUG("HttpsHandler", "Not a worker script, skipping patch: " + url_);
    return;
  }

  LOG_INFO("HttpsHandler", ">>> PATCHING WORKER SCRIPT via WorkerPatcher: " + url_ +
           " browser_id=" + std::to_string(browser_id_));

  // Convert data_ to string for patching
  std::string content(data_.begin(), data_.end());

  // Use DedicatedWorkerPatcher for .owl domain scripts
  // These are typically test pages that may spawn nested workers via blob URLs
  // DedicatedWorkerPatcher includes both worker_script AND early_blob_script
  owl::workers::DedicatedWorkerPatcher patcher;
  std::string patched = patcher.PatchScript(content, url_, browser_id_);

  // Convert patched string back to vector<uint8_t>
  data_.assign(patched.begin(), patched.end());

  LOG_INFO("HttpsHandler", "Successfully patched via WorkerPatcher. New size: " +
           std::to_string(data_.size()) + " bytes");
}

// ============================================================================
// OwlHttpsSchemeHandlerFactory - Factory for HTTPS .owl domain handlers
// ============================================================================

OwlHttpsSchemeHandlerFactory::OwlHttpsSchemeHandlerFactory() {
  LOG_DEBUG("HttpsSchemeFactory", "Factory constructor called");
}

CefRefPtr<CefResourceHandler> OwlHttpsSchemeHandlerFactory::Create(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& scheme_name,
    CefRefPtr<CefRequest> request) {

  std::string url = request->GetURL().ToString();
  std::string scheme = scheme_name.ToString();

  LOG_DEBUG("HttpsSchemeFactory", "######################################");
  LOG_DEBUG("HttpsSchemeFactory", "### HTTPS FACTORY CREATE() CALLED! ###");
  LOG_DEBUG("HttpsSchemeFactory", "URL: " + url);
  LOG_DEBUG("HttpsSchemeFactory", "Scheme: " + scheme);

  // Extract domain from URL
  size_t protocol_end = url.find("://");
  if (protocol_end == std::string::npos) {
    LOG_ERROR("HttpsSchemeFactory", "Invalid URL format: " + url);
    return nullptr;
  }
  protocol_end += 3;

  size_t domain_end = url.find('/', protocol_end);
  if (domain_end == std::string::npos) {
    domain_end = url.length();
  }

  std::string domain = url.substr(protocol_end, domain_end - protocol_end);
  LOG_DEBUG("HttpsSchemeFactory", "Domain: " + domain);

  // Get browser_id for VM profile lookup
  int browser_id = browser.get() ? browser->GetIdentifier() : 0;

  // Create handler for .owl domain
  CefRefPtr<CefResourceHandler> handler = new OwlHttpsResourceHandler(domain, url, browser_id);

  LOG_DEBUG("HttpsSchemeFactory", "Handler created, returning");
  LOG_DEBUG("HttpsSchemeFactory", "######################################");

  return handler;
}
