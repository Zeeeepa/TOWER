#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

// Proxy type enumeration
enum class ProxyType {
  NONE,      // No proxy (direct connection)
  HTTP,      // HTTP proxy
  HTTPS,     // HTTPS proxy
  SOCKS4,    // SOCKS4 proxy
  SOCKS5,    // SOCKS5 proxy (with authentication support)
  SOCKS5H    // SOCKS5 with remote DNS resolution (most stealthy)
};

// Proxy configuration structure
struct ProxyConfig {
  ProxyType type = ProxyType::NONE;
  std::string host;
  int port = 0;
  std::string username;       // For authenticated proxies
  std::string password;       // For authenticated proxies
  bool enabled = false;       // Is proxy active?

  // Stealth settings
  bool stealth_mode = true;         // Enable all stealth features
  bool block_webrtc = true;         // Block WebRTC to prevent IP leaks
  bool spoof_timezone = true;       // Match timezone to proxy location
  bool spoof_language = true;       // Match language to proxy location
  bool randomize_fingerprint = true; // Add noise to fingerprinting

  // Advanced stealth
  std::string timezone_override;     // e.g., "America/New_York"
  std::string language_override;     // e.g., "en-US"
  std::string geolocation_override;  // JSON with lat/lng for geolocation API

  // Connection settings
  int connect_timeout_ms = 30000;    // Proxy connection timeout
  int request_timeout_ms = 60000;    // Request timeout through proxy
  bool verify_ssl = true;            // Verify SSL certificates

  // Custom CA certificate for SSL interception proxies (e.g., Charles Proxy, mitmproxy)
  std::string ca_cert_path;          // Path to custom CA certificate file (.pem, .crt, .cer)
  bool trust_custom_ca = false;      // Enable trusting the custom CA certificate

  // Tor-specific settings for circuit isolation
  // When using Tor, each browser context can get a different exit node (IP)
  // by requesting a new circuit via Tor's control port before creating the context
  int tor_control_port = 0;          // Tor control port (0 = auto-detect 9051, -1 = disabled)
  std::string tor_control_password;  // Password for Tor control port (empty = try cookie auth)
  bool is_tor = false;               // Explicitly mark as Tor proxy (auto-detected if false)

  // Helper methods
  bool IsValid() const {
    return type != ProxyType::NONE && !host.empty() && port > 0 && port < 65536;
  }

  // Detect if this is likely a Tor proxy based on configuration
  bool IsTorProxy() const {
    // Explicitly marked as Tor
    if (is_tor) return true;

    // Auto-detect: SOCKS5H on localhost port 9050 or 9150 (Tor Browser)
    if (type == ProxyType::SOCKS5H || type == ProxyType::SOCKS5) {
      bool is_localhost = (host == "127.0.0.1" || host == "localhost" || host == "::1");
      bool is_tor_port = (port == 9050 || port == 9150);
      if (is_localhost && is_tor_port) return true;
    }

    return false;
  }

  // Get the control port to use (auto-detect or explicit)
  int GetTorControlPort() const {
    if (tor_control_port > 0) return tor_control_port;
    if (tor_control_port == -1) return 0;  // Disabled
    // Auto-detect: use 9051 for Tor daemon, 9151 for Tor Browser
    if (port == 9150) return 9151;  // Tor Browser
    return 9051;  // Standard Tor daemon
  }

  std::string GetProxyString() const;      // Returns "type://host:port" format
  std::string GetCEFProxyString() const;   // Returns CEF-compatible proxy string
};

// Proxy connection status
enum class ProxyStatus {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  ERROR,
  AUTHENTICATING
};

// Proxy manager class - handles all proxy operations
class OwlProxyManager {
public:
  // Singleton access
  static OwlProxyManager* GetInstance();

  // Configuration
  void SetProxyConfig(const ProxyConfig& config);
  ProxyConfig GetProxyConfig() const;

  // Connection management
  bool Connect();
  void Disconnect();
  ProxyStatus GetStatus() const;
  std::string GetStatusMessage() const;

  // Proxy URL generation for CEF
  // Returns the proxy URL in format suitable for CefRequestContextSettings
  std::string GetCEFProxyURL() const;

  // Proxy validation
  bool TestProxy(const ProxyConfig& config, int timeout_ms = 10000);
  bool ValidateProxy() const;

  // Stealth features
  void ApplyStealthSettings(const ProxyConfig& config);
  std::string GetTimezoneForProxy() const;
  std::string GetLanguageForProxy() const;
  std::string GetGeolocationForProxy() const;

  // IP lookup
  std::string GetPublicIP() const;
  std::string GetProxiedIP() const;  // Get IP as seen through proxy

  // Statistics
  int64_t GetBytesUploaded() const { return bytes_uploaded_.load(); }
  int64_t GetBytesDownloaded() const { return bytes_downloaded_.load(); }
  int GetConnectionCount() const { return connection_count_.load(); }

  // Serialization
  std::string ToJSON() const;
  static ProxyConfig FromJSON(const std::string& json);

  // Type conversion helpers
  static std::string ProxyTypeToString(ProxyType type);
  static ProxyType StringToProxyType(const std::string& type_str);
  static std::string ProxyStatusToString(ProxyStatus status);

  // Tor circuit isolation
  // Request a new Tor circuit (NEWNYM) via the control port
  // This gives a new exit node IP for subsequent connections
  // Returns true if successful, false if failed (e.g., control port not available)
  static bool RequestNewTorCircuit(const ProxyConfig& config);

  // Check if Tor control port is available
  static bool IsTorControlPortAvailable(int control_port);

private:
  OwlProxyManager();
  ~OwlProxyManager();

  // Prevent copying
  OwlProxyManager(const OwlProxyManager&) = delete;
  OwlProxyManager& operator=(const OwlProxyManager&) = delete;

  static OwlProxyManager* instance_;
  static std::mutex instance_mutex_;

  // Configuration
  ProxyConfig config_;
  mutable std::mutex config_mutex_;

  // Status
  std::atomic<ProxyStatus> status_;
  std::string status_message_;
  mutable std::mutex status_mutex_;

  // Statistics
  std::atomic<int64_t> bytes_uploaded_;
  std::atomic<int64_t> bytes_downloaded_;
  std::atomic<int> connection_count_;

  // IP caching
  mutable std::string cached_public_ip_;
  mutable std::string cached_proxied_ip_;

  // Helper methods
  bool ValidateHostPort(const std::string& host, int port) const;
  std::string LookupTimezone(const std::string& ip) const;
  std::string LookupLanguage(const std::string& ip) const;
};
