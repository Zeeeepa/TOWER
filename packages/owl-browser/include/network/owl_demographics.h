#pragma once

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include "owl_proxy_manager.h"

// Forward declaration for MaxMind DB
struct MMDB_s;

// Demographics and Context Information Provider
// Provides location, time, and weather context for AI-enhanced browsing

struct DateTimeInfo {
  std::string current_datetime;      // ISO 8601: "2025-10-23T14:30:00Z"
  std::string timezone;              // e.g., "America/New_York"
  std::string timezone_offset;       // e.g., "-04:00"
  std::string day_of_week;           // e.g., "Wednesday"
  std::string date;                  // e.g., "2025-10-23"
  std::string time;                  // e.g., "14:30:00"
  int64_t unix_timestamp;            // Seconds since epoch
};

struct GeoLocationInfo {
  std::string ip_address;            // Public IP
  std::string country_code;          // ISO code: "US"
  std::string country_name;          // "United States"
  std::string city;                  // "New York"
  std::string region;                // State/Province
  std::string postal_code;           // ZIP code
  double latitude;                   // e.g., 40.7128
  double longitude;                  // e.g., -74.0060
  std::string timezone;              // e.g., "America/New_York"
  bool success;
  std::string error;
};

struct WeatherInfo {
  double temperature_celsius;
  double temperature_fahrenheit;
  std::string condition;             // "Sunny", "Cloudy", "Rainy", etc.
  std::string description;           // Detailed description
  double humidity_percent;           // 0-100
  double wind_speed_kmh;
  double wind_speed_mph;
  int weather_code;                  // WMO weather code
  bool success;
  std::string error;
};

struct DemographicInfo {
  DateTimeInfo datetime;
  GeoLocationInfo location;
  WeatherInfo weather;
  bool has_location;                 // Whether location was successfully determined
  bool has_weather;                  // Whether weather was successfully fetched
};

class OwlDemographics {
public:
  // Get singleton instance
  static OwlDemographics* GetInstance();

  // Initialize the MaxMind database
  bool Initialize(const std::string& mmdb_path = "");

  // Shutdown and cleanup
  void Shutdown();

  // Get all demographic information at once
  DemographicInfo GetAllInfo();

  // Individual getters (cached for performance)
  DateTimeInfo GetDateTime();
  GeoLocationInfo GetGeoLocation();
  WeatherInfo GetWeather();

  // Manual IP override (for testing or manual configuration)
  void SetIPAddress(const std::string& ip);

  // Set proxy configuration for HTTP requests
  // When set, demographics will detect IP through the proxy
  void SetProxyConfig(const ProxyConfig& config);
  void ClearProxyConfig();

  // Clear cached data to force refresh
  void ClearCache();

  // Check if system is ready
  bool IsReady() const { return initialized_; }

  // Serialize to XML for LLM prompts
  static std::string ToXML(const DemographicInfo& info);
  static std::string ToJSON(const DemographicInfo& info);

private:
  OwlDemographics();
  ~OwlDemographics();

  // Prevent copying
  OwlDemographics(const OwlDemographics&) = delete;
  OwlDemographics& operator=(const OwlDemographics&) = delete;

  // Internal implementation
  std::string DetectPublicIP();
  GeoLocationInfo LookupGeoLocation(const std::string& ip);
  WeatherInfo FetchWeather(double latitude, double longitude);

  // HTTP helper (uses proxy if configured)
  std::string HttpGet(const std::string& url);

  // Singleton instance
  static OwlDemographics* instance_;
  static std::mutex instance_mutex_;

  // Proxy configuration for HTTP requests
  ProxyConfig proxy_config_;
  std::mutex proxy_mutex_;

  // MaxMind DB handle
  MMDB_s* mmdb_;
  std::string mmdb_path_;
  bool initialized_;

  // Caching
  std::mutex cache_mutex_;
  std::string cached_ip_;
  GeoLocationInfo cached_location_;
  WeatherInfo cached_weather_;
  std::chrono::steady_clock::time_point last_ip_check_;
  std::chrono::steady_clock::time_point last_weather_check_;

  // Cache durations
  static constexpr int IP_CACHE_SECONDS = 300;      // 5 minutes
  static constexpr int WEATHER_CACHE_SECONDS = 600; // 10 minutes
};
