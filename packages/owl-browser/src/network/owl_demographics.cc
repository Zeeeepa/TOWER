#include "owl_demographics.h"
#include "owl_platform_utils.h"
#include "logger.h"
#include <curl/curl.h>
#include <maxminddb.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <regex>
#include <fstream>
#include <vector>
#include <unistd.h>
#include "include/cef_parser.h"

// Static members
OwlDemographics* OwlDemographics::instance_ = nullptr;
std::mutex OwlDemographics::instance_mutex_;

// ============================================================
// Singleton Management
// ============================================================

OwlDemographics* OwlDemographics::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = new OwlDemographics();
  }
  return instance_;
}

OwlDemographics::OwlDemographics()
    : mmdb_(nullptr), initialized_(false) {
  LOG_DEBUG("Demographics", "Demographics system created");
}

OwlDemographics::~OwlDemographics() {
  Shutdown();
}

// ============================================================
// Initialization
// ============================================================

bool OwlDemographics::Initialize(const std::string& mmdb_path) {
  std::lock_guard<std::mutex> lock(cache_mutex_);

  if (initialized_) {
    LOG_WARN("Demographics", "Already initialized");
    return true;
  }

  // Determine MMDB path
  if (!mmdb_path.empty()) {
    mmdb_path_ = mmdb_path;
  } else {
    // Get resources directory using cross-platform function
    std::string resources_dir = OlibPlatform::GetResourcesDir();
    std::string resource_path;

    if (!resources_dir.empty()) {
      resource_path = resources_dir + "/third_party/GeoLite2-City.mmdb";
      LOG_DEBUG("Demographics", "Constructed resource path: " + resource_path);
    }

    // Try multiple paths for the database file
    std::vector<std::string> possible_paths;

    // Add the dynamically constructed path first
    if (!resource_path.empty()) {
      possible_paths.push_back(resource_path);
    }

    // Fallback paths
    possible_paths.push_back("/usr/local/share/owl-browser/third_party/GeoLite2-City.mmdb");  // Linux install
    possible_paths.push_back("Release/owl_browser_ui.app/Contents/Resources/third_party/GeoLite2-City.mmdb");  // From build dir
    possible_paths.push_back("../Resources/third_party/GeoLite2-City.mmdb");  // macOS app bundle (when run from MacOS dir)
    possible_paths.push_back("third_party/GeoLite2-City.mmdb");  // Development build

    bool found = false;
    for (const auto& path : possible_paths) {
      LOG_DEBUG("Demographics", "Testing path: " + path);
      std::ifstream test(path);
      if (test.good()) {
        LOG_DEBUG("Demographics", "Found database at: " + path);
        mmdb_path_ = path;
        found = true;
        break;
      } else {
        LOG_DEBUG("Demographics", "Path not accessible: " + path);
      }
    }

    if (!found) {
      LOG_WARN("Demographics", "Database not found in any standard location, using fallback");
      mmdb_path_ = "third_party/GeoLite2-City.mmdb";  // Fallback to default
    }
  }

  LOG_DEBUG("Demographics", "Initializing MaxMind DB: " + mmdb_path_);

  // Open MaxMind database
  mmdb_ = new MMDB_s();
  int status = MMDB_open(mmdb_path_.c_str(), MMDB_MODE_MMAP, mmdb_);

  if (status != MMDB_SUCCESS) {
    LOG_ERROR("Demographics", "Failed to open MaxMind DB: " + std::string(MMDB_strerror(status)));
    delete mmdb_;
    mmdb_ = nullptr;
    return false;
  }

  LOG_DEBUG("Demographics", "MaxMind DB opened successfully");
  initialized_ = true;
  return true;
}

void OwlDemographics::Shutdown() {
  std::lock_guard<std::mutex> lock(cache_mutex_);

  if (mmdb_) {
    MMDB_close(mmdb_);
    delete mmdb_;
    mmdb_ = nullptr;
  }

  initialized_ = false;
  LOG_DEBUG("Demographics", "Demographics system shutdown");
}

// ============================================================
// Public API
// ============================================================

DemographicInfo OwlDemographics::GetAllInfo() {
  DemographicInfo info;

  // Always get datetime (no network required)
  info.datetime = GetDateTime();

  // Get geolocation (requires network and MaxMind DB)
  info.location = GetGeoLocation();
  info.has_location = info.location.success;

  // Get weather (requires location)
  if (info.has_location) {
    info.weather = GetWeather();
    info.has_weather = info.weather.success;
  } else {
    info.has_weather = false;
    info.weather.success = false;
    info.weather.error = "Location not available";
  }

  return info;
}

DateTimeInfo OwlDemographics::GetDateTime() {
  DateTimeInfo info;

  // Get current time
  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);

  // Unix timestamp
  info.unix_timestamp = static_cast<int64_t>(now_t);

  // UTC time
  std::tm* utc_tm = std::gmtime(&now_t);

  // ISO 8601 format
  std::ostringstream datetime_stream;
  datetime_stream << std::put_time(utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  info.current_datetime = datetime_stream.str();

  // Date
  std::ostringstream date_stream;
  date_stream << std::put_time(utc_tm, "%Y-%m-%d");
  info.date = date_stream.str();

  // Time
  std::ostringstream time_stream;
  time_stream << std::put_time(utc_tm, "%H:%M:%S");
  info.time = time_stream.str();

  // Day of week
  const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  info.day_of_week = days[utc_tm->tm_wday];

  // Timezone (default to UTC, will be overridden by geolocation if available)
  info.timezone = "UTC";
  info.timezone_offset = "+00:00";

  return info;
}

GeoLocationInfo OwlDemographics::GetGeoLocation() {
  // Check initialization BEFORE acquiring lock to avoid deadlock
  if (!initialized_) {
    LOG_WARN("Demographics", "Not initialized, attempting auto-initialization");
    if (!Initialize()) {
      GeoLocationInfo info;
      info.success = false;
      info.error = "MaxMind DB not initialized";
      return info;
    }
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);

  // Check cache
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ip_check_).count();

  if (!cached_ip_.empty() && elapsed < IP_CACHE_SECONDS && cached_location_.success) {
    LOG_DEBUG("Demographics", "Using cached geolocation (age: " + std::to_string(elapsed) + "s)");
    return cached_location_;
  }

  // Detect public IP
  std::string ip = DetectPublicIP();
  if (ip.empty()) {
    GeoLocationInfo info;
    info.success = false;
    info.error = "Failed to detect public IP";
    return info;
  }

  cached_ip_ = ip;
  last_ip_check_ = now;

  // Lookup geolocation
  cached_location_ = LookupGeoLocation(ip);
  return cached_location_;
}

WeatherInfo OwlDemographics::GetWeather() {
  // First get location BEFORE acquiring lock to avoid deadlock
  // (GetGeoLocation also acquires cache_mutex_)
  GeoLocationInfo location = GetGeoLocation();
  if (!location.success) {
    WeatherInfo info;
    info.success = false;
    info.error = "Location not available for weather lookup";
    return info;
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);

  // Check cache
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_weather_check_).count();

  if (cached_weather_.success && elapsed < WEATHER_CACHE_SECONDS) {
    LOG_DEBUG("Demographics", "Using cached weather (age: " + std::to_string(elapsed) + "s)");
    return cached_weather_;
  }

  // Fetch weather
  cached_weather_ = FetchWeather(location.latitude, location.longitude);
  last_weather_check_ = now;
  return cached_weather_;
}

void OwlDemographics::SetIPAddress(const std::string& ip) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  cached_ip_ = ip;
  last_ip_check_ = std::chrono::steady_clock::now();
  LOG_DEBUG("Demographics", "Manual IP set: " + ip);
}

void OwlDemographics::SetProxyConfig(const ProxyConfig& config) {
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    proxy_config_ = config;
  }
  // Clear cache so next request uses new proxy
  ClearCache();
  if (config.IsValid() && config.enabled) {
    LOG_DEBUG("Demographics", "Proxy configured: " + config.GetProxyString());
  } else {
    LOG_DEBUG("Demographics", "Proxy cleared - using direct connection");
  }
}

void OwlDemographics::ClearProxyConfig() {
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    proxy_config_ = ProxyConfig();
  }
  // Clear cache so next request uses direct connection
  ClearCache();
  LOG_DEBUG("Demographics", "Proxy cleared - using direct connection");
}

void OwlDemographics::ClearCache() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  cached_ip_.clear();
  cached_location_ = GeoLocationInfo();
  cached_weather_ = WeatherInfo();
  LOG_DEBUG("Demographics", "Cache cleared");
}

// ============================================================
// Internal Implementation
// ============================================================

std::string OwlDemographics::DetectPublicIP() {
  LOG_DEBUG("Demographics", "Detecting public IP...");

  // Try multiple IP detection services for reliability
  const char* services[] = {
    "https://api.ipify.org",
    "https://icanhazip.com",
    "https://ifconfig.me/ip"
  };

  for (const char* service : services) {
    std::string response = HttpGet(service);

    // Trim whitespace
    response.erase(0, response.find_first_not_of(" \n\r\t"));
    response.erase(response.find_last_not_of(" \n\r\t") + 1);

    // Validate IP format (basic check for IPv4)
    std::regex ipv4_pattern(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)");
    if (std::regex_match(response, ipv4_pattern)) {
      LOG_DEBUG("Demographics", "Detected public IP: " + response);
      return response;
    }
  }

  LOG_ERROR("Demographics", "Failed to detect public IP from all services");
  return "";
}

GeoLocationInfo OwlDemographics::LookupGeoLocation(const std::string& ip) {
  GeoLocationInfo info;
  info.ip_address = ip;

  if (!mmdb_) {
    info.success = false;
    info.error = "MaxMind DB not loaded";
    return info;
  }

  LOG_DEBUG("Demographics", "Looking up geolocation for IP: " + ip);

  // Lookup IP in database
  int gai_error, mmdb_error;
  MMDB_lookup_result_s result = MMDB_lookup_string(mmdb_, ip.c_str(), &gai_error, &mmdb_error);

  if (gai_error != 0) {
    info.success = false;
    info.error = "GAI error: " + std::string(gai_strerror(gai_error));
    LOG_ERROR("Demographics", info.error);
    return info;
  }

  if (mmdb_error != MMDB_SUCCESS) {
    info.success = false;
    info.error = "MMDB error: " + std::string(MMDB_strerror(mmdb_error));
    LOG_ERROR("Demographics", info.error);
    return info;
  }

  if (!result.found_entry) {
    info.success = false;
    info.error = "IP not found in database";
    LOG_WARN("Demographics", "IP not found in GeoLite2 database: " + ip);
    return info;
  }

  // Extract country
  MMDB_entry_data_s entry_data;
  int status;

  status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    info.country_code = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  status = MMDB_get_value(&result.entry, &entry_data, "country", "names", "en", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    info.country_name = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  // Extract city
  status = MMDB_get_value(&result.entry, &entry_data, "city", "names", "en", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    info.city = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  // Extract region/subdivision
  status = MMDB_get_value(&result.entry, &entry_data, "subdivisions", "0", "names", "en", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    info.region = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  // Extract postal code
  status = MMDB_get_value(&result.entry, &entry_data, "postal", "code", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    info.postal_code = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  // Extract coordinates
  status = MMDB_get_value(&result.entry, &entry_data, "location", "latitude", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_DOUBLE) {
    info.latitude = entry_data.double_value;
  }

  status = MMDB_get_value(&result.entry, &entry_data, "location", "longitude", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_DOUBLE) {
    info.longitude = entry_data.double_value;
  }

  // Extract timezone
  status = MMDB_get_value(&result.entry, &entry_data, "location", "time_zone", NULL);
  if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    info.timezone = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  info.success = true;
  LOG_DEBUG("Demographics", "Geolocation found: " + info.city + ", " + info.country_name);
  return info;
}

WeatherInfo OwlDemographics::FetchWeather(double latitude, double longitude) {
  WeatherInfo info;

  LOG_DEBUG("Demographics", "Fetching weather for coordinates: " +
           std::to_string(latitude) + ", " + std::to_string(longitude));

  // Use Open-Meteo free API (no API key required)
  // https://open-meteo.com/en/docs
  std::ostringstream url_stream;
  url_stream << "https://api.open-meteo.com/v1/forecast?"
             << "latitude=" << latitude
             << "&longitude=" << longitude
             << "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
             << "&temperature_unit=celsius"
             << "&wind_speed_unit=kmh";

  std::string response = HttpGet(url_stream.str());
  if (response.empty()) {
    info.success = false;
    info.error = "Failed to fetch weather data";
    return info;
  }

  // Parse JSON response
  CefRefPtr<CefValue> json_value = CefParseJSON(response, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  if (!json_value || json_value->GetType() != VTYPE_DICTIONARY) {
    info.success = false;
    info.error = "Failed to parse weather JSON";
    return info;
  }

  CefRefPtr<CefDictionaryValue> dict = json_value->GetDictionary();

  if (!dict->HasKey("current")) {
    info.success = false;
    info.error = "No current weather data in response";
    return info;
  }

  CefRefPtr<CefDictionaryValue> current = dict->GetDictionary("current");

  // Extract temperature
  if (current->HasKey("temperature_2m")) {
    info.temperature_celsius = current->GetDouble("temperature_2m");
    info.temperature_fahrenheit = (info.temperature_celsius * 9.0 / 5.0) + 32.0;
  }

  // Extract humidity
  if (current->HasKey("relative_humidity_2m")) {
    info.humidity_percent = current->GetDouble("relative_humidity_2m");
  }

  // Extract wind speed
  if (current->HasKey("wind_speed_10m")) {
    info.wind_speed_kmh = current->GetDouble("wind_speed_10m");
    info.wind_speed_mph = info.wind_speed_kmh * 0.621371;
  }

  // Extract weather code
  if (current->HasKey("weather_code")) {
    info.weather_code = current->GetInt("weather_code");

    // Convert WMO weather code to human-readable condition
    // https://open-meteo.com/en/docs#weathervariables
    if (info.weather_code == 0) {
      info.condition = "Clear";
      info.description = "Clear sky";
    } else if (info.weather_code <= 3) {
      info.condition = "Partly Cloudy";
      info.description = "Mainly clear to partly cloudy";
    } else if (info.weather_code <= 49) {
      info.condition = "Foggy";
      info.description = "Fog or mist";
    } else if (info.weather_code <= 69) {
      info.condition = "Rainy";
      info.description = "Rain or drizzle";
    } else if (info.weather_code <= 79) {
      info.condition = "Snowy";
      info.description = "Snow";
    } else if (info.weather_code <= 84) {
      info.condition = "Showers";
      info.description = "Rain showers";
    } else {
      info.condition = "Stormy";
      info.description = "Thunderstorm";
    }
  }

  info.success = true;
  LOG_DEBUG("Demographics", "Weather: " + info.condition + ", " +
           std::to_string((int)info.temperature_celsius) + "°C");
  return info;
}

// ============================================================
// HTTP Helper (using CURL)
// ============================================================

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

std::string OwlDemographics::HttpGet(const std::string& url) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    LOG_ERROR("Demographics", "Failed to initialize CURL");
    return "";
  }

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // 10 second timeout
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // Follow redirects
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Don't verify SSL (for simplicity)
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

  // Apply proxy settings if configured
  ProxyConfig proxy;
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    proxy = proxy_config_;
  }

  if (proxy.IsValid() && proxy.enabled) {
    // Set proxy URL (host:port)
    std::string proxy_url = proxy.host + ":" + std::to_string(proxy.port);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy_url.c_str());

    // Set proxy type
    long proxy_type = CURLPROXY_HTTP;
    switch (proxy.type) {
      case ProxyType::HTTP:
        proxy_type = CURLPROXY_HTTP;
        break;
      case ProxyType::HTTPS:
        proxy_type = CURLPROXY_HTTPS;
        break;
      case ProxyType::SOCKS4:
        proxy_type = CURLPROXY_SOCKS4;
        break;
      case ProxyType::SOCKS5:
        proxy_type = CURLPROXY_SOCKS5;
        break;
      case ProxyType::SOCKS5H:
        proxy_type = CURLPROXY_SOCKS5_HOSTNAME;  // Remote DNS resolution
        break;
      default:
        break;
    }
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, proxy_type);

    // Set proxy authentication if provided
    if (!proxy.username.empty()) {
      curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, proxy.username.c_str());
      if (!proxy.password.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, proxy.password.c_str());
      }
    }

    LOG_DEBUG("Demographics", "HTTP request via proxy: " + proxy.GetProxyString());
  }

  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    LOG_ERROR("Demographics", "CURL error: " + std::string(curl_easy_strerror(res)));
    response.clear();
  }

  curl_easy_cleanup(curl);
  return response;
}

// ============================================================
// Serialization
// ============================================================

std::string OwlDemographics::ToXML(const DemographicInfo& info) {
  std::ostringstream xml;
  xml << "<demographics>\n";

  // DateTime
  xml << "  <datetime>\n";
  xml << "    <current>" << info.datetime.current_datetime << "</current>\n";
  xml << "    <date>" << info.datetime.date << "</date>\n";
  xml << "    <time>" << info.datetime.time << "</time>\n";
  xml << "    <day_of_week>" << info.datetime.day_of_week << "</day_of_week>\n";
  xml << "    <timezone>" << (info.has_location ? info.location.timezone : info.datetime.timezone) << "</timezone>\n";
  xml << "  </datetime>\n";

  // Location
  if (info.has_location) {
    xml << "  <location>\n";
    xml << "    <city>" << info.location.city << "</city>\n";
    xml << "    <region>" << info.location.region << "</region>\n";
    xml << "    <country>" << info.location.country_name << "</country>\n";
    xml << "    <country_code>" << info.location.country_code << "</country_code>\n";
    xml << "    <latitude>" << info.location.latitude << "</latitude>\n";
    xml << "    <longitude>" << info.location.longitude << "</longitude>\n";
    xml << "  </location>\n";
  }

  // Weather
  if (info.has_weather) {
    xml << "  <weather>\n";
    xml << "    <condition>" << info.weather.condition << "</condition>\n";
    xml << "    <description>" << info.weather.description << "</description>\n";
    xml << "    <temperature_c>" << std::fixed << std::setprecision(1) << info.weather.temperature_celsius << "</temperature_c>\n";
    xml << "    <temperature_f>" << std::fixed << std::setprecision(1) << info.weather.temperature_fahrenheit << "</temperature_f>\n";
    xml << "    <humidity>" << info.weather.humidity_percent << "</humidity>\n";
    xml << "  </weather>\n";
  }

  xml << "</demographics>";
  return xml.str();
}

std::string OwlDemographics::ToJSON(const DemographicInfo& info) {
  std::ostringstream json;
  json << "{";

  // DateTime
  json << "\"datetime\":{";
  json << "\"current\":\"" << info.datetime.current_datetime << "\",";
  json << "\"date\":\"" << info.datetime.date << "\",";
  json << "\"time\":\"" << info.datetime.time << "\",";
  json << "\"day_of_week\":\"" << info.datetime.day_of_week << "\",";
  json << "\"timezone\":\"" << (info.has_location ? info.location.timezone : info.datetime.timezone) << "\"";
  json << "}";

  // Location
  if (info.has_location) {
    json << ",\"location\":{";
    json << "\"city\":\"" << info.location.city << "\",";
    json << "\"region\":\"" << info.location.region << "\",";
    json << "\"country\":\"" << info.location.country_name << "\",";
    json << "\"country_code\":\"" << info.location.country_code << "\",";
    json << "\"latitude\":" << info.location.latitude << ",";
    json << "\"longitude\":" << info.location.longitude;
    json << "}";
  }

  // Weather
  if (info.has_weather) {
    json << ",\"weather\":{";
    json << "\"condition\":\"" << info.weather.condition << "\",";
    json << "\"description\":\"" << info.weather.description << "\",";
    json << "\"temperature_c\":" << std::fixed << std::setprecision(1) << info.weather.temperature_celsius << ",";
    json << "\"temperature_f\":" << std::fixed << std::setprecision(1) << info.weather.temperature_fahrenheit << ",";
    json << "\"humidity\":" << info.weather.humidity_percent;
    json << "}";
  }

  json << "}";
  return json.str();
}
