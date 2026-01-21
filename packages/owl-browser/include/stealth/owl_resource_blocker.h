#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <regex>
#include "include/cef_request.h"
#include "include/cef_resource_request_handler.h"

// AI-first resource blocker for maximum performance
// Blocks ads, analytics, trackers automatically
class OwlResourceBlocker {
public:
  static OwlResourceBlocker* GetInstance();

  void Initialize();

  // Check if a URL should be blocked
  bool ShouldBlockRequest(const std::string& url, const std::string& resource_type);

  // Block categories
  bool IsAdDomain(const std::string& domain);
  bool IsAnalyticsDomain(const std::string& domain);
  bool IsTrackerDomain(const std::string& domain);

  // Statistics for AI
  struct BlockStats {
    int ads_blocked;
    int analytics_blocked;
    int trackers_blocked;
    int total_blocked;
    int total_requests;
    double block_percentage;
  };

  BlockStats GetStats() const;
  void ResetStats();

private:
  OwlResourceBlocker();
  ~OwlResourceBlocker();

  static OwlResourceBlocker* instance_;
  static std::mutex instance_mutex_;

  // Blocklists - simple and fast hash sets
  std::unordered_set<std::string> ad_domains_;
  std::unordered_set<std::string> analytics_domains_;
  std::unordered_set<std::string> tracker_domains_;

  // Fast pattern matching for common patterns (using regex for 10-100x speedup)
  std::vector<std::string> ad_patterns_;
  std::vector<std::string> analytics_patterns_;
  std::regex ad_pattern_regex_;
  std::regex analytics_pattern_regex_;
  bool regex_initialized_;

  // Statistics
  mutable std::mutex stats_mutex_;
  int ads_blocked_;
  int analytics_blocked_;
  int trackers_blocked_;
  int total_requests_;

  // Helper methods
  std::string ExtractDomain(const std::string& url);
  bool MatchesPattern(const std::string& url, const std::vector<std::string>& patterns);
  bool MatchesRegexPattern(const std::string& url, const std::regex& pattern);
  void LoadBuiltInBlocklists();
  void CompileRegexPatterns();
};
