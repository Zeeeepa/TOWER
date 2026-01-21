#ifndef OWL_DEV_ELEMENTS_H
#define OWL_DEV_ELEMENTS_H

#include <string>
#include <vector>
#include <mutex>

#ifdef __cplusplus
extern "C++" {
#endif

// DOM Element structure for Elements tab
struct DOMElement {
  std::string tag;
  std::string id;
  std::string classes;
  std::string text_content;
  int depth;
  std::vector<std::pair<std::string, std::string>> attributes;
};

// Elements tab handler for developer console
// Captures and displays DOM tree structure
class OwlDevElements {
public:
  OwlDevElements();
  ~OwlDevElements();

  // Add DOM element to the tree
  void AddElement(const DOMElement& element);

  // Clear all elements
  void ClearElements();

  // Get HTML content for the Elements tab
  std::string GenerateHTML();

  // Get JSON representation of DOM tree
  std::string GetDOMTreeJSON();

private:
  std::vector<DOMElement> elements_;
  std::mutex elements_mutex_;
};

#ifdef __cplusplus
}
#endif

#endif // OWL_DEV_ELEMENTS_H
