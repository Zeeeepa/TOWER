#ifndef OWL_HOMEPAGE_H
#define OWL_HOMEPAGE_H

#include <string>

// Forward declarations
class OwlBrowserManager;

/**
 * @class OwlHomepage
 * @brief Generates a beautiful custom browser homepage with demographics,
 *        weather, LLM status, and browser information.
 *
 * All HTML/CSS/JS is inline for self-contained rendering.
 */
class OwlHomepage {
 public:
  /**
   * Generate the complete HTML homepage
   * @param manager Pointer to browser manager for accessing demographics/LLM status
   * @return Complete HTML string with inline CSS
   */
  static std::string GenerateHomepage(OwlBrowserManager* manager);

 private:
  /**
   * Generate the HTML header with inline CSS
   */
  static std::string GenerateHeader();

  /**
   * Generate the browser info bar with location, time, weather, IP
   */
  static std::string GenerateBrowserInfoBar(OwlBrowserManager* manager);

  /**
   * Generate the demographics/weather card section
   */
  static std::string GenerateDemographicsCard(OwlBrowserManager* manager);

  /**
   * Generate the LLM status card section
   */
  static std::string GenerateLLMStatusCard(OwlBrowserManager* manager);

  /**
   * Generate the browser info card section
   */
  static std::string GenerateBrowserInfoCard();

  /**
   * Generate the footer with inline JavaScript
   */
  static std::string GenerateFooter(OwlBrowserManager* manager);
};

#endif  // OWL_HOMEPAGE_H
