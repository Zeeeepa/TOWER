#ifndef OWL_PLAYGROUND_H
#define OWL_PLAYGROUND_H

#include <string>

// Forward declarations
class OwlBrowserManager;

/**
 * @class OwlPlayground
 * @brief Generates the Developer Playground UI for building and testing automation flows.
 *
 * The playground allows developers to:
 * - Build test flows interactively (add steps: navigate, click, type, etc.)
 * - Execute test flows and see them happen in the main browser window
 * - Export tests as JSON templates for use with the SDK/MCP
 *
 * All HTML/CSS/JS is inline for self-contained rendering.
 */
class OwlPlayground {
 public:
  /**
   * Generate the complete HTML playground
   * @param manager Pointer to browser manager for accessing browser features
   * @return Complete HTML string with inline CSS and JS
   */
  static std::string GeneratePlayground(OwlBrowserManager* manager);

 private:
  /**
   * Generate the HTML header with inline CSS
   */
  static std::string GenerateHeader();

  /**
   * Generate the main playground UI (step builder, controls, etc.)
   */
  static std::string GeneratePlaygroundUI();

  /**
   * Generate the footer with inline JavaScript
   */
  static std::string GenerateFooter();
};

#endif  // OWL_PLAYGROUND_H
