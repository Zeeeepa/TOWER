#ifndef OWL_DEV_NETWORK_H
#define OWL_DEV_NETWORK_H

#include <string>
#include <vector>
#include <mutex>
#include <chrono>

#ifdef __cplusplus
extern "C++" {
#endif

// Network request structure for Network tab
struct NetworkRequest {
  std::string url;
  std::string method;
  std::string type;  // Document, XHR, Script, Stylesheet, Image, etc.
  int status_code;
  std::string status_text;
  size_t size;  // Response size in bytes
  std::chrono::milliseconds duration;
  std::chrono::system_clock::time_point timestamp;
  std::string request_headers;
  std::string response_headers;
};

// Network tab handler for developer console
// Captures and displays network requests
class OwlDevNetwork {
public:
  OwlDevNetwork();
  ~OwlDevNetwork();

  // Add network request
  void AddRequest(const NetworkRequest& request);

  // Clear all requests
  void ClearRequests();

  // Get HTML content for the Network tab
  std::string GenerateHTML();

  // Get JSON representation of network requests
  std::string GetRequestsJSON();

private:
  std::vector<NetworkRequest> requests_;
  std::mutex requests_mutex_;

  // Helper to format file size
  std::string FormatSize(size_t bytes);

  // Helper to format duration
  std::string FormatDuration(std::chrono::milliseconds ms);
};

#ifdef __cplusplus
}
#endif

#endif // OWL_DEV_NETWORK_H
