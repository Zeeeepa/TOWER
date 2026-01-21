#include "owl_proxy_manager.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <thread>
#include <regex>
#include <fstream>
#include <iomanip>

// Socket includes for Tor control port communication
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
  #define CLOSE_SOCKET closesocket
  #define SOCKET_ERROR_CODE WSAGetLastError()
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  typedef int socket_t;
  #define INVALID_SOCKET -1
  #define CLOSE_SOCKET close
  #define SOCKET_ERROR_CODE errno
#endif

// Static member initialization
OwlProxyManager* OwlProxyManager::instance_ = nullptr;
std::mutex OwlProxyManager::instance_mutex_;

// ProxyConfig methods
std::string ProxyConfig::GetProxyString() const {
  if (!IsValid()) return "";

  std::string prefix;
  switch (type) {
    case ProxyType::HTTP:    prefix = "http://"; break;
    case ProxyType::HTTPS:   prefix = "https://"; break;
    case ProxyType::SOCKS4:  prefix = "socks4://"; break;
    case ProxyType::SOCKS5:  prefix = "socks5://"; break;
    case ProxyType::SOCKS5H: prefix = "socks5h://"; break;
    default: return "";
  }

  std::ostringstream oss;
  oss << prefix;

  // Add authentication if provided
  if (!username.empty()) {
    oss << username;
    if (!password.empty()) {
      oss << ":" << password;
    }
    oss << "@";
  }

  oss << host << ":" << port;
  return oss.str();
}

std::string ProxyConfig::GetCEFProxyString() const {
  if (!IsValid()) return "";

  // CEF/Chromium proxy rules format:
  // For HTTP proxy: "host:port" or "http=host:port"
  // For SOCKS: "socks=host:port" (SOCKS5) or "socks4=host:port"
  // Note: Using scheme=host:port format works better with CEF preferences

  std::ostringstream oss;
  switch (type) {
    case ProxyType::HTTP:
    case ProxyType::HTTPS:
      // HTTP proxy - just use host:port format
      oss << host << ":" << port;
      break;
    case ProxyType::SOCKS4:
      oss << "socks4=" << host << ":" << port;
      break;
    case ProxyType::SOCKS5:
    case ProxyType::SOCKS5H:
      // SOCKS5 - use socks= prefix (not socks5://)
      oss << "socks=" << host << ":" << port;
      break;
    default:
      return "";
  }

  return oss.str();
}

// OwlProxyManager implementation
OwlProxyManager::OwlProxyManager()
    : status_(ProxyStatus::DISCONNECTED),
      bytes_uploaded_(0),
      bytes_downloaded_(0),
      connection_count_(0) {
  LOG_DEBUG("ProxyManager", "Proxy manager initialized");
}

OwlProxyManager::~OwlProxyManager() {
  Disconnect();
  LOG_DEBUG("ProxyManager", "Proxy manager destroyed");
}

OwlProxyManager* OwlProxyManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = new OwlProxyManager();
  }
  return instance_;
}

void OwlProxyManager::SetProxyConfig(const ProxyConfig& config) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  config_ = config;

  if (config.IsValid()) {
    LOG_DEBUG("ProxyManager", "Proxy configured: " + ProxyTypeToString(config.type) +
             " " + config.host + ":" + std::to_string(config.port) +
             " (stealth: " + (config.stealth_mode ? "enabled" : "disabled") + ")");
  } else {
    LOG_DEBUG("ProxyManager", "Proxy disabled");
  }
}

ProxyConfig OwlProxyManager::GetProxyConfig() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return config_;
}

bool OwlProxyManager::Connect() {
  ProxyConfig config;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config = config_;
  }

  if (!config.IsValid()) {
    LOG_ERROR("ProxyManager", "Cannot connect - invalid proxy configuration");
    status_ = ProxyStatus::ERROR;
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_message_ = "Invalid proxy configuration";
    }
    return false;
  }

  status_ = ProxyStatus::CONNECTING;
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_message_ = "Connecting to " + config.host + ":" + std::to_string(config.port);
  }

  LOG_DEBUG("ProxyManager", "Connecting to proxy: " + config.GetProxyString());

  // Test the proxy connection
  if (!TestProxy(config, config.connect_timeout_ms)) {
    status_ = ProxyStatus::ERROR;
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_message_ = "Failed to connect to proxy";
    }
    LOG_ERROR("ProxyManager", "Proxy connection test failed");
    return false;
  }

  // Apply stealth settings if enabled
  if (config.stealth_mode) {
    ApplyStealthSettings(config);
  }

  status_ = ProxyStatus::CONNECTED;
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_message_ = "Connected via " + ProxyTypeToString(config.type);
  }
  connection_count_++;

  LOG_DEBUG("ProxyManager", "Successfully connected to proxy");
  return true;
}

void OwlProxyManager::Disconnect() {
  ProxyStatus expected = ProxyStatus::CONNECTED;
  if (status_.compare_exchange_strong(expected, ProxyStatus::DISCONNECTED)) {
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_message_ = "Disconnected";
    }
    LOG_DEBUG("ProxyManager", "Disconnected from proxy");

    // Clear cached IPs
    {
      std::lock_guard<std::mutex> lock(config_mutex_);
      cached_proxied_ip_.clear();
    }
  }
}

ProxyStatus OwlProxyManager::GetStatus() const {
  return status_.load();
}

std::string OwlProxyManager::GetStatusMessage() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_message_;
}

std::string OwlProxyManager::GetCEFProxyURL() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  if (!config_.enabled || !config_.IsValid()) {
    return "";  // Direct connection
  }
  return config_.GetCEFProxyString();
}

bool OwlProxyManager::TestProxy(const ProxyConfig& config, int timeout_ms) {
  // Simple validation - actual connection test would require network calls
  // For stealth purposes, we don't want to make obvious proxy test requests

  if (!config.IsValid()) {
    return false;
  }

  // Validate host format (no obvious injection)
  if (!ValidateHostPort(config.host, config.port)) {
    LOG_ERROR("ProxyManager", "Invalid host/port format");
    return false;
  }

  LOG_DEBUG("ProxyManager", "Proxy configuration validated: " + config.GetProxyString());
  return true;
}

bool OwlProxyManager::ValidateProxy() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return config_.IsValid() && config_.enabled;
}

void OwlProxyManager::ApplyStealthSettings(const ProxyConfig& config) {
  LOG_DEBUG("ProxyManager", "Applying stealth settings...");

  // These settings will be used by OwlStealth when injecting JavaScript
  // The actual injection happens in the browser context

  if (config.block_webrtc) {
    LOG_DEBUG("ProxyManager", "WebRTC blocking enabled");
  }

  if (config.spoof_timezone && !config.timezone_override.empty()) {
    LOG_DEBUG("ProxyManager", "Timezone spoofing: " + config.timezone_override);
  }

  if (config.spoof_language && !config.language_override.empty()) {
    LOG_DEBUG("ProxyManager", "Language spoofing: " + config.language_override);
  }

  if (config.randomize_fingerprint) {
    LOG_DEBUG("ProxyManager", "Fingerprint randomization enabled");
  }

  LOG_DEBUG("ProxyManager", "Stealth settings applied");
}

std::string OwlProxyManager::GetTimezoneForProxy() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  if (!config_.timezone_override.empty()) {
    return config_.timezone_override;
  }
  // Default - could implement GeoIP lookup here
  return "";
}

std::string OwlProxyManager::GetLanguageForProxy() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  if (!config_.language_override.empty()) {
    return config_.language_override;
  }
  // Default - could implement GeoIP lookup here
  return "";
}

std::string OwlProxyManager::GetGeolocationForProxy() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return config_.geolocation_override;
}

std::string OwlProxyManager::GetPublicIP() const {
  // Return cached IP if available
  if (!cached_public_ip_.empty()) {
    return cached_public_ip_;
  }
  // Actual IP lookup would be done via HTTP request
  return "";
}

std::string OwlProxyManager::GetProxiedIP() const {
  if (!cached_proxied_ip_.empty()) {
    return cached_proxied_ip_;
  }
  return "";
}

std::string OwlProxyManager::ToJSON() const {
  std::lock_guard<std::mutex> lock(config_mutex_);

  std::ostringstream json;
  json << "{";
  json << "\"enabled\":" << (config_.enabled ? "true" : "false");
  json << ",\"type\":\"" << ProxyTypeToString(config_.type) << "\"";
  json << ",\"host\":\"" << config_.host << "\"";
  json << ",\"port\":" << config_.port;
  json << ",\"username\":\"" << config_.username << "\"";
  // Don't include password in JSON for security
  json << ",\"hasPassword\":" << (!config_.password.empty() ? "true" : "false");
  json << ",\"stealthMode\":" << (config_.stealth_mode ? "true" : "false");
  json << ",\"blockWebrtc\":" << (config_.block_webrtc ? "true" : "false");
  json << ",\"spoofTimezone\":" << (config_.spoof_timezone ? "true" : "false");
  json << ",\"spoofLanguage\":" << (config_.spoof_language ? "true" : "false");
  json << ",\"randomizeFingerprint\":" << (config_.randomize_fingerprint ? "true" : "false");

  if (!config_.timezone_override.empty()) {
    json << ",\"timezoneOverride\":\"" << config_.timezone_override << "\"";
  }
  if (!config_.language_override.empty()) {
    json << ",\"languageOverride\":\"" << config_.language_override << "\"";
  }

  // CA certificate settings
  json << ",\"trustCustomCa\":" << (config_.trust_custom_ca ? "true" : "false");
  if (!config_.ca_cert_path.empty()) {
    json << ",\"caCertPath\":\"" << config_.ca_cert_path << "\"";
  }

  json << ",\"status\":\"" << ProxyStatusToString(status_.load()) << "\"";
  json << ",\"statusMessage\":\"" << GetStatusMessage() << "\"";
  json << ",\"bytesUploaded\":" << bytes_uploaded_.load();
  json << ",\"bytesDownloaded\":" << bytes_downloaded_.load();
  json << ",\"connectionCount\":" << connection_count_.load();
  json << "}";

  return json.str();
}

ProxyConfig OwlProxyManager::FromJSON(const std::string& json) {
  ProxyConfig config;

  // Simple JSON parsing (production should use proper JSON library)
  auto extractBool = [&json](const std::string& key) -> bool {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos != std::string::npos) {
      pos += search.length();
      while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
      return json.substr(pos, 4) == "true";
    }
    return false;
  };

  auto extractString = [&json](const std::string& key) -> std::string {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos != std::string::npos) {
      pos += search.length();
      size_t end = json.find('"', pos);
      if (end != std::string::npos) {
        return json.substr(pos, end - pos);
      }
    }
    return "";
  };

  auto extractInt = [&json](const std::string& key) -> int {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos != std::string::npos) {
      pos += search.length();
      while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
      size_t end = json.find_first_of(",}", pos);
      if (end != std::string::npos) {
        std::string numStr = json.substr(pos, end - pos);
        // Simple integer parsing without exceptions
        int result = 0;
        bool negative = false;
        size_t i = 0;
        if (!numStr.empty() && numStr[0] == '-') {
          negative = true;
          i = 1;
        }
        for (; i < numStr.size(); ++i) {
          if (numStr[i] >= '0' && numStr[i] <= '9') {
            result = result * 10 + (numStr[i] - '0');
          } else {
            break;
          }
        }
        return negative ? -result : result;
      }
    }
    return 0;
  };

  config.enabled = extractBool("enabled");
  config.type = StringToProxyType(extractString("type"));
  config.host = extractString("host");
  config.port = extractInt("port");
  config.username = extractString("username");
  config.password = extractString("password");
  config.stealth_mode = extractBool("stealthMode");
  config.block_webrtc = extractBool("blockWebrtc");
  config.spoof_timezone = extractBool("spoofTimezone");
  config.spoof_language = extractBool("spoofLanguage");
  config.randomize_fingerprint = extractBool("randomizeFingerprint");
  config.timezone_override = extractString("timezoneOverride");
  config.language_override = extractString("languageOverride");
  config.geolocation_override = extractString("geolocationOverride");

  // CA certificate settings
  config.trust_custom_ca = extractBool("trustCustomCa");
  config.ca_cert_path = extractString("caCertPath");

  return config;
}

std::string OwlProxyManager::ProxyTypeToString(ProxyType type) {
  switch (type) {
    case ProxyType::NONE:    return "none";
    case ProxyType::HTTP:    return "http";
    case ProxyType::HTTPS:   return "https";
    case ProxyType::SOCKS4:  return "socks4";
    case ProxyType::SOCKS5:  return "socks5";
    case ProxyType::SOCKS5H: return "socks5h";
    default: return "unknown";
  }
}

ProxyType OwlProxyManager::StringToProxyType(const std::string& type_str) {
  if (type_str == "http") return ProxyType::HTTP;
  if (type_str == "https") return ProxyType::HTTPS;
  if (type_str == "socks4") return ProxyType::SOCKS4;
  if (type_str == "socks5") return ProxyType::SOCKS5;
  if (type_str == "socks5h") return ProxyType::SOCKS5H;
  return ProxyType::NONE;
}

std::string OwlProxyManager::ProxyStatusToString(ProxyStatus status) {
  switch (status) {
    case ProxyStatus::DISCONNECTED:   return "disconnected";
    case ProxyStatus::CONNECTING:     return "connecting";
    case ProxyStatus::CONNECTED:      return "connected";
    case ProxyStatus::ERROR:          return "error";
    case ProxyStatus::AUTHENTICATING: return "authenticating";
    default: return "unknown";
  }
}

bool OwlProxyManager::ValidateHostPort(const std::string& host, int port) const {
  // Validate port
  if (port <= 0 || port > 65535) {
    return false;
  }

  // Validate host - prevent obvious injection attempts
  if (host.empty() || host.length() > 255) {
    return false;
  }

  // Check for invalid characters
  for (char c : host) {
    if (!std::isalnum(c) && c != '.' && c != '-' && c != ':' && c != '[' && c != ']') {
      return false;
    }
  }

  // Basic hostname/IP validation
  // IPv4: digits and dots
  // IPv6: hex digits, colons, brackets
  // Hostname: alphanumeric and hyphens

  // Prevent localhost bypasses
  std::string lower_host = host;
  for (auto& c : lower_host) c = std::tolower(c);

  if (lower_host == "localhost" || lower_host == "127.0.0.1" ||
      lower_host == "::1" || lower_host == "[::1]") {
    // Allow localhost for testing
    return true;
  }

  return true;
}

std::string OwlProxyManager::LookupTimezone(const std::string& ip) const {
  // Would implement GeoIP timezone lookup
  // For now, return empty to use browser default
  return "";
}

std::string OwlProxyManager::LookupLanguage(const std::string& ip) const {
  // Would implement GeoIP language lookup
  // For now, return empty to use browser default
  return "";
}

// ============================================================================
// Tor Circuit Isolation
// ============================================================================

bool OwlProxyManager::IsTorControlPortAvailable(int control_port) {
  if (control_port <= 0) return false;

  socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCKET) {
    return false;
  }

  // Set socket to non-blocking for quick timeout
  #ifndef _WIN32
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  #endif

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(control_port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));

  #ifndef _WIN32
    if (result < 0 && errno == EINPROGRESS) {
      // Wait for connection with timeout
      fd_set write_fds;
      FD_ZERO(&write_fds);
      FD_SET(sock, &write_fds);

      struct timeval tv;
      tv.tv_sec = 1;  // 1 second timeout
      tv.tv_usec = 0;

      result = select(sock + 1, nullptr, &write_fds, nullptr, &tv);
      if (result > 0) {
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
        result = (error == 0) ? 0 : -1;
      } else {
        result = -1;
      }
    }
  #endif

  CLOSE_SOCKET(sock);
  return (result == 0);
}

bool OwlProxyManager::RequestNewTorCircuit(const ProxyConfig& config) {
  if (!config.IsTorProxy()) {
    LOG_DEBUG("TorControl", "Not a Tor proxy, skipping circuit request");
    return false;
  }

  int control_port = config.GetTorControlPort();
  if (control_port <= 0) {
    LOG_DEBUG("TorControl", "Tor control port disabled");
    return false;
  }

  LOG_DEBUG("TorControl", "Requesting new Tor circuit via control port " + std::to_string(control_port));

  // Create socket
  socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCKET) {
    LOG_ERROR("TorControl", "Failed to create socket: " + std::to_string(SOCKET_ERROR_CODE));
    return false;
  }

  // Connect to control port
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(control_port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    LOG_WARN("TorControl", "Failed to connect to Tor control port " + std::to_string(control_port) +
             ". Make sure Tor is running with ControlPort enabled.");
    CLOSE_SOCKET(sock);
    return false;
  }

  LOG_DEBUG("TorControl", "Connected to Tor control port");

  // Buffer for reading responses
  char buffer[1024];
  ssize_t bytes_read;

  // Read welcome message (if any)
  #ifndef _WIN32
    // Set read timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  #endif

  // Try to authenticate
  std::string auth_cmd;
  if (!config.tor_control_password.empty()) {
    // Password authentication
    auth_cmd = "AUTHENTICATE \"" + config.tor_control_password + "\"\r\n";
    LOG_DEBUG("TorControl", "Using password authentication");
  } else {
    // Try cookie authentication first, fall back to no-auth
    // Cookie file is typically at ~/.tor/control_auth_cookie or /var/lib/tor/control_auth_cookie
    std::string cookie_data;
    std::vector<std::string> cookie_paths = {
      std::string(getenv("HOME") ? getenv("HOME") : "") + "/.tor/control_auth_cookie",
      "/var/lib/tor/control_auth_cookie",
      "/var/run/tor/control.authcookie",
      std::string(getenv("HOME") ? getenv("HOME") : "") + "/Library/Application Support/TorBrowser-Data/Tor/control_auth_cookie"
    };

    for (const auto& path : cookie_paths) {
      std::ifstream cookie_file(path, std::ios::binary);
      if (cookie_file.good()) {
        std::stringstream ss;
        ss << cookie_file.rdbuf();
        cookie_data = ss.str();
        LOG_DEBUG("TorControl", "Found cookie file at: " + path);
        break;
      }
    }

    if (!cookie_data.empty()) {
      // Convert cookie to hex
      std::stringstream hex_cookie;
      for (unsigned char c : cookie_data) {
        hex_cookie << std::hex << std::setfill('0') << std::setw(2) << (int)c;
      }
      auth_cmd = "AUTHENTICATE " + hex_cookie.str() + "\r\n";
      LOG_DEBUG("TorControl", "Using cookie authentication");
    } else {
      // Try no authentication (Tor might be configured with no auth)
      auth_cmd = "AUTHENTICATE\r\n";
      LOG_DEBUG("TorControl", "Trying no authentication");
    }
  }

  // Send AUTHENTICATE command
  if (send(sock, auth_cmd.c_str(), auth_cmd.length(), 0) < 0) {
    LOG_ERROR("TorControl", "Failed to send AUTHENTICATE command");
    CLOSE_SOCKET(sock);
    return false;
  }

  // Read response
  memset(buffer, 0, sizeof(buffer));
  bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read <= 0) {
    LOG_ERROR("TorControl", "No response to AUTHENTICATE command");
    CLOSE_SOCKET(sock);
    return false;
  }

  std::string auth_response(buffer);
  LOG_DEBUG("TorControl", "Auth response: " + auth_response.substr(0, 50));

  if (auth_response.find("250") != 0) {
    LOG_ERROR("TorControl", "Authentication failed: " + auth_response);
    CLOSE_SOCKET(sock);
    return false;
  }

  LOG_DEBUG("TorControl", "Authentication successful");

  // Send SIGNAL NEWNYM command to request a new circuit
  std::string newnym_cmd = "SIGNAL NEWNYM\r\n";
  if (send(sock, newnym_cmd.c_str(), newnym_cmd.length(), 0) < 0) {
    LOG_ERROR("TorControl", "Failed to send SIGNAL NEWNYM command");
    CLOSE_SOCKET(sock);
    return false;
  }

  // Read response
  memset(buffer, 0, sizeof(buffer));
  bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read <= 0) {
    LOG_ERROR("TorControl", "No response to SIGNAL NEWNYM command");
    CLOSE_SOCKET(sock);
    return false;
  }

  std::string newnym_response(buffer);
  LOG_DEBUG("TorControl", "NEWNYM response: " + newnym_response.substr(0, 50));

  // Send QUIT command
  std::string quit_cmd = "QUIT\r\n";
  send(sock, quit_cmd.c_str(), quit_cmd.length(), 0);

  CLOSE_SOCKET(sock);

  if (newnym_response.find("250") == 0) {
    LOG_DEBUG("TorControl", "Successfully requested new Tor circuit (new exit node)");
    // Tor rate-limits NEWNYM to once per 10 seconds
    // Sleep briefly to ensure the new circuit is established
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return true;
  } else {
    LOG_WARN("TorControl", "NEWNYM command failed: " + newnym_response);
    return false;
  }
}
