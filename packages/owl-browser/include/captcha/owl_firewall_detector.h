#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "include/cef_browser.h"

/**
 * FirewallDetector - Detects web firewall/bot protection challenges during navigation
 *
 * Unlike form CAPTCHAs, web firewalls present full-page interstitials that block access
 * to the target content. This detector identifies:
 *
 * 1. Cloudflare - "Just a moment...", Turnstile, JS challenges
 * 2. Akamai Bot Manager - Bot detection pages
 * 3. Imperva/Incapsula - Security challenge pages
 * 4. PerimeterX - Human challenge screens
 * 5. DataDome - Bot detection interstitials
 * 6. AWS WAF - Block pages
 * 7. Sucuri - Firewall challenges
 * 8. DDoS-Guard - Challenge pages
 * 9. Generic bot protection pages
 *
 * Detection strategies:
 * - Title pattern matching (high confidence)
 * - Provider-specific script/iframe detection
 * - Page structure analysis (minimal content indicates challenge)
 * - Meta tag analysis (noindex, security headers)
 * - Text content patterns ("checking your browser", "verify you are human")
 */

/**
 * Known web firewall/bot protection providers
 */
enum class FirewallProvider {
  NONE,                    // No firewall detected
  CLOUDFLARE,              // Cloudflare JS challenge
  CLOUDFLARE_TURNSTILE,    // Cloudflare Turnstile CAPTCHA
  CLOUDFLARE_MANAGED,      // Cloudflare Managed Challenge
  AKAMAI,                  // Akamai Bot Manager
  IMPERVA,                 // Imperva/Incapsula
  PERIMETERX,              // PerimeterX
  DATADOME,                // DataDome
  AWS_WAF,                 // AWS Web Application Firewall
  SUCURI,                  // Sucuri CloudProxy
  DDOS_GUARD,              // DDoS-Guard
  DISTIL,                  // Distil Networks (now Imperva)
  KASADA,                  // Kasada
  SHAPE,                   // Shape Security (now F5)
  REBLAZE,                 // Reblaze
  GENERIC                  // Generic/unknown firewall
};

/**
 * Type of challenge presented by the firewall
 */
enum class FirewallChallengeType {
  NONE,                    // No challenge
  JS_CHALLENGE,            // JavaScript verification (may auto-solve)
  MANAGED_CHALLENGE,       // Managed/invisible challenge
  CAPTCHA,                 // CAPTCHA required (Turnstile, reCAPTCHA, etc.)
  INTERSTITIAL,            // Full-page wait screen
  HARD_BLOCK,              // Access denied (no challenge possible)
  RATE_LIMIT               // Rate limiting page
};

/**
 * Result of firewall detection
 */
struct FirewallDetectionResult {
  bool detected;                          // True if firewall challenge detected
  FirewallProvider provider;              // Detected provider
  FirewallChallengeType challenge_type;   // Type of challenge
  double confidence;                       // Detection confidence (0.0 - 1.0)
  std::vector<std::string> indicators;    // List of detection signals found
  std::string provider_name;              // Human-readable provider name
  std::string challenge_description;      // Description of the challenge
  std::string ray_id;                     // Cloudflare Ray ID if available
  bool may_auto_solve;                    // True if JS challenge may auto-solve
  int estimated_wait_seconds;             // Estimated wait time for JS challenges

  FirewallDetectionResult()
    : detected(false),
      provider(FirewallProvider::NONE),
      challenge_type(FirewallChallengeType::NONE),
      confidence(0.0),
      may_auto_solve(false),
      estimated_wait_seconds(0) {}
};

/**
 * Web firewall/bot protection detector
 */
class OwlFirewallDetector {
public:
  OwlFirewallDetector();
  ~OwlFirewallDetector();

  /**
   * Detect if the current page is a web firewall challenge
   * Should be called after navigation completes to check if we hit a challenge
   *
   * @param browser The CEF browser instance
   * @return Detection result with provider, challenge type, and confidence
   */
  FirewallDetectionResult Detect(CefRefPtr<CefBrowser> browser);

  /**
   * Quick check for common firewall patterns (faster, less thorough)
   * Use this for initial screening, then call Detect() for full analysis
   *
   * @param browser The CEF browser instance
   * @return True if firewall likely detected
   */
  bool QuickCheck(CefRefPtr<CefBrowser> browser);

  /**
   * Get human-readable name for a provider
   */
  static std::string GetProviderName(FirewallProvider provider);

  /**
   * Get human-readable description for a challenge type
   */
  static std::string GetChallengeDescription(FirewallChallengeType type);

  /**
   * Check if a challenge type may auto-solve (JS challenges often do)
   */
  static bool MayAutoSolve(FirewallChallengeType type);

private:
  /**
   * Detect based on page title patterns
   */
  bool DetectByTitle(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect Cloudflare-specific patterns
   */
  bool DetectCloudflare(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect Akamai Bot Manager patterns
   */
  bool DetectAkamai(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect Imperva/Incapsula patterns
   */
  bool DetectImperva(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect PerimeterX patterns
   */
  bool DetectPerimeterX(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect DataDome patterns
   */
  bool DetectDataDome(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect AWS WAF patterns
   */
  bool DetectAWSWAF(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Detect generic firewall/bot protection patterns
   */
  bool DetectGeneric(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Analyze page structure for challenge indicators
   * (Minimal content, no main elements, challenge scripts)
   */
  bool AnalyzePageStructure(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Check for challenge-related meta tags
   */
  bool CheckMetaTags(CefRefPtr<CefBrowser> browser, FirewallDetectionResult& result);

  /**
   * Extract Cloudflare Ray ID if present
   */
  std::string ExtractRayId(CefRefPtr<CefBrowser> browser);

  // Title patterns that indicate firewall challenges (lowercase for matching)
  std::vector<std::string> challenge_titles_;

  // Body text patterns indicating challenges
  std::vector<std::string> challenge_text_patterns_;

  // Provider-specific identifiers
  std::unordered_map<std::string, FirewallProvider> script_patterns_;
  std::unordered_map<std::string, FirewallProvider> iframe_patterns_;
  std::unordered_map<std::string, FirewallProvider> element_patterns_;
};
