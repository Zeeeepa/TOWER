#include "owl_firewall_detector.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <regex>

namespace {
  // Helper to convert string to lowercase
  std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
  }
}

OwlFirewallDetector::OwlFirewallDetector() {
  // Title patterns indicating firewall challenges (already lowercase)
  challenge_titles_ = {
    // Cloudflare
    "just a moment",
    "attention required",
    "checking your browser",
    "please wait",
    "one more step",
    "security check",
    "ddos protection",
    "access denied",
    "error 1020",
    "error 1015",
    "error 1012",
    "ray id",

    // Akamai
    "access denied",
    "bot manager",
    "akamai",

    // Imperva/Incapsula
    "incapsula incident",
    "request unsuccessful",
    "pardon our interruption",

    // PerimeterX
    "human challenge",
    "press & hold",
    "px-captcha",

    // DataDome
    "blocked",
    "captcha-delivery",

    // Generic
    "bot detected",
    "are you a robot",
    "verify you're human",
    "enable javascript",
    "enable cookies",
    "automated access",
    "unusual traffic",
    "too many requests",
    "rate limit"
  };

  // Text patterns in body content
  challenge_text_patterns_ = {
    // Cloudflare patterns
    "checking your browser before accessing",
    "this process is automatic",
    "please enable javascript",
    "please enable cookies",
    "complete the security check",
    "why do i have to complete a captcha",
    "ray id:",
    "cloudflare ray id",
    "performance & security by cloudflare",
    "please stand by",
    "ddos protection by",
    "cf-browser-verification",

    // Akamai patterns
    "access to this page has been denied",
    "reference#",
    "your request was blocked",

    // Imperva/Incapsula patterns
    "incident id",
    "powered by incapsula",
    "request unsuccessful. incapsula",

    // PerimeterX patterns
    "press and hold to confirm",
    "blocked by px",

    // DataDome patterns
    "blocked by datadome",
    "human verification required",

    // Generic patterns
    "please verify you are a human",
    "we need to verify",
    "unusual activity",
    "automated queries",
    "you have been blocked",
    "access has been blocked",
    "your ip has been",
    "rate limited",
    "too many requests from your ip"
  };

  // Script source patterns -> provider mapping
  script_patterns_ = {
    // Cloudflare
    {"challenges.cloudflare.com", FirewallProvider::CLOUDFLARE_TURNSTILE},
    {"cf-challenge", FirewallProvider::CLOUDFLARE},
    {"__cf_chl", FirewallProvider::CLOUDFLARE},
    {"cdn-cgi/challenge-platform", FirewallProvider::CLOUDFLARE},
    {"turnstile", FirewallProvider::CLOUDFLARE_TURNSTILE},
    {"cf.min.js", FirewallProvider::CLOUDFLARE},
    {"beacon.min.js", FirewallProvider::CLOUDFLARE},

    // Akamai
    {"akam/", FirewallProvider::AKAMAI},
    {"akamai", FirewallProvider::AKAMAI},
    {"_abck", FirewallProvider::AKAMAI},
    {"ak_bmsc", FirewallProvider::AKAMAI},
    {"bm-verify", FirewallProvider::AKAMAI},

    // Imperva/Incapsula
    {"incapsula", FirewallProvider::IMPERVA},
    {"reese84", FirewallProvider::IMPERVA},
    {"_imp_apg_r_", FirewallProvider::IMPERVA},

    // PerimeterX
    {"px-captcha", FirewallProvider::PERIMETERX},
    {"captcha.px-cloud", FirewallProvider::PERIMETERX},
    {"px-cdn.net", FirewallProvider::PERIMETERX},
    {"/px/", FirewallProvider::PERIMETERX},

    // DataDome
    {"datadome", FirewallProvider::DATADOME},
    {"captcha-delivery.com", FirewallProvider::DATADOME},
    {"dd.js", FirewallProvider::DATADOME},
    {"tags.datadome.co", FirewallProvider::DATADOME},

    // Kasada
    {"ips.js", FirewallProvider::KASADA},
    {"kasada", FirewallProvider::KASADA},

    // Shape/F5
    {"shape", FirewallProvider::SHAPE},
    {"f5-shape", FirewallProvider::SHAPE}
  };

  // Iframe source patterns -> provider mapping
  iframe_patterns_ = {
    {"challenges.cloudflare.com", FirewallProvider::CLOUDFLARE_TURNSTILE},
    {"hcaptcha.com", FirewallProvider::CLOUDFLARE},  // Cloudflare sometimes uses hCaptcha
    {"recaptcha/api", FirewallProvider::GENERIC},
    {"captcha-delivery.com", FirewallProvider::DATADOME},
    {"px-captcha", FirewallProvider::PERIMETERX},
    {"geo.captcha-delivery.com", FirewallProvider::DATADOME}
  };

  // Element ID/class patterns -> provider mapping
  element_patterns_ = {
    // Cloudflare
    {"cf-browser-verification", FirewallProvider::CLOUDFLARE},
    {"cf-wrapper", FirewallProvider::CLOUDFLARE},
    {"cf-challenge-running", FirewallProvider::CLOUDFLARE},
    {"cf-turnstile", FirewallProvider::CLOUDFLARE_TURNSTILE},
    {"cf-chl-widget", FirewallProvider::CLOUDFLARE_TURNSTILE},
    {"challenge-platform", FirewallProvider::CLOUDFLARE},
    {"challenge-form", FirewallProvider::CLOUDFLARE},

    // PerimeterX
    {"px-captcha", FirewallProvider::PERIMETERX},
    {"px-captcha-container", FirewallProvider::PERIMETERX},

    // DataDome
    {"datadome", FirewallProvider::DATADOME},
    {"dd_captcha", FirewallProvider::DATADOME},

    // Incapsula
    {"incapsula", FirewallProvider::IMPERVA},

    // Generic
    {"captcha-container", FirewallProvider::GENERIC},
    {"bot-protection", FirewallProvider::GENERIC},
    {"challenge-container", FirewallProvider::GENERIC}
  };

  LOG_DEBUG("FirewallDetector", "Initialized with " +
           std::to_string(challenge_titles_.size()) + " title patterns, " +
           std::to_string(script_patterns_.size()) + " script patterns");
}

OwlFirewallDetector::~OwlFirewallDetector() {}

std::string OwlFirewallDetector::GetProviderName(FirewallProvider provider) {
  switch (provider) {
    case FirewallProvider::NONE: return "None";
    case FirewallProvider::CLOUDFLARE: return "Cloudflare";
    case FirewallProvider::CLOUDFLARE_TURNSTILE: return "Cloudflare Turnstile";
    case FirewallProvider::CLOUDFLARE_MANAGED: return "Cloudflare Managed Challenge";
    case FirewallProvider::AKAMAI: return "Akamai Bot Manager";
    case FirewallProvider::IMPERVA: return "Imperva/Incapsula";
    case FirewallProvider::PERIMETERX: return "PerimeterX";
    case FirewallProvider::DATADOME: return "DataDome";
    case FirewallProvider::AWS_WAF: return "AWS WAF";
    case FirewallProvider::SUCURI: return "Sucuri CloudProxy";
    case FirewallProvider::DDOS_GUARD: return "DDoS-Guard";
    case FirewallProvider::DISTIL: return "Distil Networks";
    case FirewallProvider::KASADA: return "Kasada";
    case FirewallProvider::SHAPE: return "Shape Security (F5)";
    case FirewallProvider::REBLAZE: return "Reblaze";
    case FirewallProvider::GENERIC: return "Generic Bot Protection";
    default: return "Unknown";
  }
}

std::string OwlFirewallDetector::GetChallengeDescription(FirewallChallengeType type) {
  switch (type) {
    case FirewallChallengeType::NONE: return "No challenge";
    case FirewallChallengeType::JS_CHALLENGE: return "JavaScript challenge (may auto-solve)";
    case FirewallChallengeType::MANAGED_CHALLENGE: return "Managed/invisible challenge";
    case FirewallChallengeType::CAPTCHA: return "CAPTCHA required";
    case FirewallChallengeType::INTERSTITIAL: return "Full-page interstitial";
    case FirewallChallengeType::HARD_BLOCK: return "Access denied (hard block)";
    case FirewallChallengeType::RATE_LIMIT: return "Rate limited";
    default: return "Unknown challenge";
  }
}

bool OwlFirewallDetector::MayAutoSolve(FirewallChallengeType type) {
  return type == FirewallChallengeType::JS_CHALLENGE ||
         type == FirewallChallengeType::MANAGED_CHALLENGE ||
         type == FirewallChallengeType::INTERSTITIAL;
}

bool OwlFirewallDetector::QuickCheck(CefRefPtr<CefBrowser> browser) {
  if (!browser || !browser->GetMainFrame()) {
    return false;
  }

  // Quick title check - most efficient first pass
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  std::string url = frame->GetURL().ToString();

  // Check URL for obvious challenge paths
  std::string url_lower = ToLower(url);
  if (url_lower.find("challenge") != std::string::npos ||
      url_lower.find("captcha") != std::string::npos ||
      url_lower.find("cdn-cgi") != std::string::npos) {
    LOG_DEBUG("FirewallDetector", "Quick check: URL contains challenge pattern");
    return true;
  }

  return false;
}

FirewallDetectionResult OwlFirewallDetector::Detect(CefRefPtr<CefBrowser> browser) {
  LOG_DEBUG("FirewallDetector", "Starting firewall detection");

  FirewallDetectionResult result;

  if (!browser || !browser->GetMainFrame()) {
    LOG_ERROR("FirewallDetector", "Invalid browser or frame");
    return result;
  }

  // Track detection methods for confidence scoring
  int detection_methods = 0;

  // Run detection strategies in order of reliability
  bool found_by_title = DetectByTitle(browser, result);
  if (found_by_title) detection_methods++;

  bool found_cloudflare = DetectCloudflare(browser, result);
  if (found_cloudflare) detection_methods++;

  bool found_akamai = DetectAkamai(browser, result);
  if (found_akamai) detection_methods++;

  bool found_imperva = DetectImperva(browser, result);
  if (found_imperva) detection_methods++;

  bool found_perimeterx = DetectPerimeterX(browser, result);
  if (found_perimeterx) detection_methods++;

  bool found_datadome = DetectDataDome(browser, result);
  if (found_datadome) detection_methods++;

  bool found_aws = DetectAWSWAF(browser, result);
  if (found_aws) detection_methods++;

  bool found_generic = DetectGeneric(browser, result);
  if (found_generic) detection_methods++;

  bool structure_suspicious = AnalyzePageStructure(browser, result);
  if (structure_suspicious) detection_methods++;

  bool meta_suspicious = CheckMetaTags(browser, result);
  if (meta_suspicious) detection_methods++;

  // Determine final result
  if (detection_methods > 0) {
    result.detected = true;

    // Calculate confidence based on number of detection methods
    result.confidence = std::min(1.0, 0.3 + (detection_methods * 0.15));

    // Boost confidence for specific provider detection
    if (result.provider != FirewallProvider::NONE &&
        result.provider != FirewallProvider::GENERIC) {
      result.confidence = std::min(1.0, result.confidence + 0.2);
    }

    // Set provider name
    result.provider_name = GetProviderName(result.provider);
    result.challenge_description = GetChallengeDescription(result.challenge_type);
    result.may_auto_solve = MayAutoSolve(result.challenge_type);

    // Extract Ray ID for Cloudflare
    if (result.provider == FirewallProvider::CLOUDFLARE ||
        result.provider == FirewallProvider::CLOUDFLARE_TURNSTILE ||
        result.provider == FirewallProvider::CLOUDFLARE_MANAGED) {
      result.ray_id = ExtractRayId(browser);
    }

    // Estimate wait time for JS challenges
    if (result.challenge_type == FirewallChallengeType::JS_CHALLENGE) {
      result.estimated_wait_seconds = 5;  // Cloudflare typically 5 seconds
    } else if (result.challenge_type == FirewallChallengeType::INTERSTITIAL) {
      result.estimated_wait_seconds = 3;
    }

    LOG_DEBUG("FirewallDetector", "Firewall detected: " + result.provider_name +
             " (" + result.challenge_description + ") confidence: " +
             std::to_string(result.confidence));
  } else {
    LOG_DEBUG("FirewallDetector", "No firewall detected");
  }

  return result;
}

bool OwlFirewallDetector::DetectByTitle(CefRefPtr<CefBrowser> browser,
                                         FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking page title");

  // JavaScript to get page title
  std::string script = R"(
    (function() {
      return document.title || '';
    })();
  )";

  // Note: In production, this would use proper CEF async messaging
  // For now, we'll use the title from the browser's URL/navigation info
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  // We need to check title via JavaScript execution
  // This is a simplified check - real implementation needs async callback
  std::string url = frame->GetURL().ToString();

  // Check URL for title-like patterns (fallback)
  std::string url_lower = ToLower(url);
  for (const auto& pattern : challenge_titles_) {
    if (url_lower.find(pattern) != std::string::npos) {
      result.indicators.push_back("URL contains challenge pattern: " + pattern);
      return true;
    }
  }

  return false;
}

bool OwlFirewallDetector::DetectCloudflare(CefRefPtr<CefBrowser> browser,
                                            FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for Cloudflare");

  // Detection script for Cloudflare
  std::string detection_script = R"(
    (function() {
      var indicators = [];
      var challengeType = 'none';

      // Check for Cloudflare-specific elements
      var cfElements = [
        'cf-browser-verification',
        'cf-wrapper',
        'cf-challenge-running',
        'challenge-platform',
        'cf-turnstile',
        'cf-chl-widget'
      ];

      cfElements.forEach(function(id) {
        var elem = document.getElementById(id) || document.querySelector('.' + id);
        if (elem) {
          indicators.push('Found Cloudflare element: ' + id);
          if (id.includes('turnstile') || id.includes('chl-widget')) {
            challengeType = 'captcha';
          } else {
            challengeType = 'js_challenge';
          }
        }
      });

      // Check for Cloudflare scripts
      var scripts = document.querySelectorAll('script[src]');
      scripts.forEach(function(script) {
        var src = script.src.toLowerCase();
        if (src.includes('challenges.cloudflare.com')) {
          indicators.push('Found Cloudflare Turnstile script');
          challengeType = 'captcha';
        } else if (src.includes('__cf_chl') || src.includes('cf-challenge')) {
          indicators.push('Found Cloudflare challenge script');
          if (challengeType === 'none') challengeType = 'js_challenge';
        } else if (src.includes('cdn-cgi/challenge-platform')) {
          indicators.push('Found Cloudflare challenge platform');
          if (challengeType === 'none') challengeType = 'managed';
        }
      });

      // Check for Cloudflare iframes
      var iframes = document.querySelectorAll('iframe');
      iframes.forEach(function(iframe) {
        var src = (iframe.src || '').toLowerCase();
        if (src.includes('challenges.cloudflare.com')) {
          indicators.push('Found Cloudflare Turnstile iframe');
          challengeType = 'captcha';
        }
      });

      // Check page text content
      var bodyText = (document.body ? document.body.innerText : '').toLowerCase();

      if (bodyText.includes('checking your browser')) {
        indicators.push('Found "checking your browser" text');
        if (challengeType === 'none') challengeType = 'js_challenge';
      }

      if (bodyText.includes('ray id:') || bodyText.includes('cloudflare ray id')) {
        indicators.push('Found Cloudflare Ray ID reference');
      }

      if (bodyText.includes('performance & security by cloudflare')) {
        indicators.push('Found Cloudflare attribution');
      }

      if (bodyText.includes('please wait') && bodyText.includes('automatic')) {
        indicators.push('Found automatic verification text');
        if (challengeType === 'none') challengeType = 'js_challenge';
      }

      // Check title
      var title = (document.title || '').toLowerCase();
      if (title.includes('just a moment') || title.includes('attention required')) {
        indicators.push('Found Cloudflare challenge title');
        if (challengeType === 'none') challengeType = 'interstitial';
      }

      // Check for noscript Cloudflare message
      var noscripts = document.querySelectorAll('noscript');
      noscripts.forEach(function(ns) {
        var text = (ns.textContent || '').toLowerCase();
        if (text.includes('cloudflare') || text.includes('enable javascript')) {
          indicators.push('Found Cloudflare noscript message');
        }
      });

      return {
        found: indicators.length > 0,
        indicators: indicators,
        challengeType: challengeType
      };
    })();
  )";

  // For this implementation, we'll add indicators manually
  // A full implementation would execute this script and parse the result

  // Check for Cloudflare-specific URL patterns
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  bool found = false;

  if (url_lower.find("cdn-cgi/") != std::string::npos ||
      url_lower.find("__cf_chl") != std::string::npos) {
    result.provider = FirewallProvider::CLOUDFLARE;
    result.challenge_type = FirewallChallengeType::JS_CHALLENGE;
    result.indicators.push_back("URL contains Cloudflare challenge path");
    found = true;
  }

  if (url_lower.find("challenges.cloudflare.com") != std::string::npos) {
    result.provider = FirewallProvider::CLOUDFLARE_TURNSTILE;
    result.challenge_type = FirewallChallengeType::CAPTCHA;
    result.indicators.push_back("URL is Cloudflare challenges domain");
    found = true;
  }

  return found;
}

bool OwlFirewallDetector::DetectAkamai(CefRefPtr<CefBrowser> browser,
                                        FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for Akamai");

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  if (url_lower.find("akam/") != std::string::npos ||
      url_lower.find("akamai") != std::string::npos ||
      url_lower.find("bm-verify") != std::string::npos) {
    result.provider = FirewallProvider::AKAMAI;
    result.challenge_type = FirewallChallengeType::JS_CHALLENGE;
    result.indicators.push_back("URL contains Akamai pattern");
    return true;
  }

  return false;
}

bool OwlFirewallDetector::DetectImperva(CefRefPtr<CefBrowser> browser,
                                         FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for Imperva/Incapsula");

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  if (url_lower.find("incapsula") != std::string::npos ||
      url_lower.find("reese84") != std::string::npos ||
      url_lower.find("_imp_apg") != std::string::npos) {
    result.provider = FirewallProvider::IMPERVA;
    result.challenge_type = FirewallChallengeType::JS_CHALLENGE;
    result.indicators.push_back("URL contains Imperva/Incapsula pattern");
    return true;
  }

  return false;
}

bool OwlFirewallDetector::DetectPerimeterX(CefRefPtr<CefBrowser> browser,
                                            FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for PerimeterX");

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  if (url_lower.find("px-captcha") != std::string::npos ||
      url_lower.find("px-cloud") != std::string::npos ||
      url_lower.find("px-cdn") != std::string::npos) {
    result.provider = FirewallProvider::PERIMETERX;
    result.challenge_type = FirewallChallengeType::CAPTCHA;
    result.indicators.push_back("URL contains PerimeterX pattern");
    return true;
  }

  return false;
}

bool OwlFirewallDetector::DetectDataDome(CefRefPtr<CefBrowser> browser,
                                          FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for DataDome");

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  if (url_lower.find("datadome") != std::string::npos ||
      url_lower.find("captcha-delivery.com") != std::string::npos ||
      url_lower.find("geo.captcha-delivery") != std::string::npos) {
    result.provider = FirewallProvider::DATADOME;
    result.challenge_type = FirewallChallengeType::CAPTCHA;
    result.indicators.push_back("URL contains DataDome pattern");
    return true;
  }

  return false;
}

bool OwlFirewallDetector::DetectAWSWAF(CefRefPtr<CefBrowser> browser,
                                        FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for AWS WAF");

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  if (url_lower.find("awswaf") != std::string::npos ||
      url_lower.find("aws-waf") != std::string::npos) {
    result.provider = FirewallProvider::AWS_WAF;
    result.challenge_type = FirewallChallengeType::HARD_BLOCK;
    result.indicators.push_back("URL contains AWS WAF pattern");
    return true;
  }

  return false;
}

bool OwlFirewallDetector::DetectGeneric(CefRefPtr<CefBrowser> browser,
                                         FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking for generic firewall patterns");

  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) return false;

  std::string url = frame->GetURL().ToString();
  std::string url_lower = ToLower(url);

  // Check for generic patterns
  std::vector<std::string> generic_patterns = {
    "captcha", "challenge", "blocked", "denied",
    "robot", "bot-check", "verify", "rate-limit"
  };

  for (const auto& pattern : generic_patterns) {
    if (url_lower.find(pattern) != std::string::npos) {
      if (result.provider == FirewallProvider::NONE) {
        result.provider = FirewallProvider::GENERIC;
        result.challenge_type = FirewallChallengeType::INTERSTITIAL;
      }
      result.indicators.push_back("URL contains generic challenge pattern: " + pattern);
      return true;
    }
  }

  return false;
}

bool OwlFirewallDetector::AnalyzePageStructure(CefRefPtr<CefBrowser> browser,
                                                FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Analyzing page structure");

  // Script to analyze page structure
  std::string script = R"(
    (function() {
      var indicators = [];

      // Count DOM elements (challenge pages typically have very few)
      var allElements = document.querySelectorAll('*').length;
      if (allElements < 50) {
        indicators.push('Very minimal DOM (' + allElements + ' elements)');
      }

      // Check for absence of typical content elements
      var contentElements = document.querySelectorAll('article, main, nav, footer, aside, section');
      if (contentElements.length === 0) {
        indicators.push('No typical content elements (article, main, nav, etc.)');
      }

      // Check body class for challenge indicators
      var bodyClass = (document.body ? document.body.className : '').toLowerCase();
      if (bodyClass.includes('challenge') || bodyClass.includes('blocked') || bodyClass.includes('captcha')) {
        indicators.push('Body class indicates challenge: ' + bodyClass);
      }

      // Check for centered content (challenge pages often center everything)
      var centeredDivs = document.querySelectorAll('[style*="text-align: center"], [style*="margin: auto"], .center, .centered');
      if (centeredDivs.length > 0 && allElements < 100) {
        indicators.push('Centered content with minimal DOM');
      }

      return {
        suspicious: indicators.length > 0,
        indicators: indicators
      };
    })();
  )";

  // For now, return false - full implementation needs async JS execution
  return false;
}

bool OwlFirewallDetector::CheckMetaTags(CefRefPtr<CefBrowser> browser,
                                         FirewallDetectionResult& result) {
  LOG_DEBUG("FirewallDetector", "Checking meta tags");

  std::string script = R"(
    (function() {
      var indicators = [];

      // Check robots meta tag
      var robotsMeta = document.querySelector('meta[name="robots"]');
      if (robotsMeta) {
        var content = (robotsMeta.content || '').toLowerCase();
        if (content.includes('noindex') && content.includes('nofollow')) {
          indicators.push('Meta robots: noindex,nofollow (common on challenge pages)');
        }
      }

      // Check for security-related meta tags
      var securityMetas = document.querySelectorAll('meta[name*="security"], meta[name*="verification"]');
      if (securityMetas.length > 0) {
        indicators.push('Found security/verification meta tags');
      }

      // Check refresh meta (auto-refresh common on challenge pages)
      var refreshMeta = document.querySelector('meta[http-equiv="refresh"]');
      if (refreshMeta) {
        var content = refreshMeta.content || '';
        indicators.push('Found meta refresh: ' + content);
      }

      return {
        found: indicators.length > 0,
        indicators: indicators
      };
    })();
  )";

  // For now, return false - full implementation needs async JS execution
  return false;
}

std::string OwlFirewallDetector::ExtractRayId(CefRefPtr<CefBrowser> browser) {
  std::string script = R"(
    (function() {
      // Look for Ray ID in page text
      var bodyText = document.body ? document.body.innerText : '';
      var match = bodyText.match(/Ray ID:\s*([a-f0-9]+)/i);
      if (match) {
        return match[1];
      }

      // Check for ray_id in any element
      var rayIdElem = document.querySelector('[data-ray-id], #ray-id, .ray-id');
      if (rayIdElem) {
        return rayIdElem.textContent || rayIdElem.getAttribute('data-ray-id') || '';
      }

      return '';
    })();
  )";

  // For now, return empty - full implementation needs async JS execution
  return "";
}
