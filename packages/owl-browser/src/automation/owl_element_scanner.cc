#include "owl_element_scanner.h"
#include "logger.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

// Configuration constants for scanning
namespace {
  // Base element limits - can be adjusted based on page complexity
  constexpr int kBaseElementLimit = 3000;        // Increased from 2000
  constexpr int kMaxChildrenPerNode = 300;       // Increased from 200
  constexpr int kInteractiveElementBonus = 500;  // Extra slots for interactive elements

  // Visibility thresholds
  constexpr float kMinVisibleOpacity = 0.05f;    // Lowered from 0.1 for edge cases
  constexpr float kCumulativeOpacityThreshold = 0.03f;  // For cascaded opacity
}

void OwlElementScanner::ScanAndReportElements(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const std::string& context_id) {
  CEF_REQUIRE_RENDERER_THREAD();

  LOG_DEBUG("ElementScanner", "=== DOM SCAN START === context=" + context_id);

  CefRefPtr<CefV8Context> context = frame->GetV8Context();
  if (!context || !context->Enter()) {
    LOG_ERROR("ElementScanner", "Failed to enter V8 context");
    return;
  }

  std::vector<ElementRenderInfo> elements;

  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<CefV8Value> document = global->GetValue("document");

  if (document && document->IsObject()) {
    LOG_DEBUG("ElementScanner", "Document object found, scanning body");
    // Get document.body
    CefRefPtr<CefV8Value> body = document->GetValue("body");
    if (body && body->IsObject()) {
      LOG_DEBUG("ElementScanner", "Body found, starting recursive scan");
      // Start with parent opacity of 1.0 (no parent) and depth 0
      ScanDOMTree(context, body, elements, 1.0f, 0);
    } else {
      LOG_ERROR("ElementScanner", "Body not found or not an object");
    }
  } else {
    LOG_ERROR("ElementScanner", "Document not found or not an object");
  }

#ifdef OWL_DEBUG_BUILD
  LOG_DEBUG("ElementScanner", "Scan complete. Found " + std::to_string(elements.size()) + " visible elements");

  // Log first few elements for debugging
  int count = 0;
  for (const auto& elem : elements) {
    if (count++ < 5) {
      LOG_DEBUG("ElementScanner", "  Element: " + elem.tag +
                (elem.id.empty() ? "" : "#" + elem.id) +
                " at (" + std::to_string(elem.x) + "," + std::to_string(elem.y) + ")");
    }
  }
#endif

  // Send to browser process
  SendElementsToBrowserProcess(browser, frame, context_id, elements);

  context->Exit();
  LOG_DEBUG("ElementScanner", "=== DOM SCAN END ===");
}

bool OwlElementScanner::ScanElement(CefRefPtr<CefV8Context> context, const std::string& selector, ElementRenderInfo& info) {
  CEF_REQUIRE_RENDERER_THREAD();

  if (!context || !context->Enter()) {
    return false;
  }

  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<CefV8Value> document = global->GetValue("document");

  if (!document || !document->IsObject()) {
    context->Exit();
    return false;
  }

  // Call document.querySelector
  CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
  if (!querySelector || !querySelector->IsFunction()) {
    context->Exit();
    return false;
  }

  CefV8ValueList args;
  args.push_back(CefV8Value::CreateString(selector));

  CefRefPtr<CefV8Value> element = querySelector->ExecuteFunctionWithContext(context, document, args);

  if (!element || element->IsNull() || element->IsUndefined()) {
    context->Exit();
    return false;
  }

  info = ExtractElementInfo(context, element);
  context->Exit();

  return info.visible && (info.width > 0 && info.height > 0);
}

void OwlElementScanner::ScanDOMTree(CefRefPtr<CefV8Context> context,
                                     CefRefPtr<CefV8Value> element,
                                     std::vector<ElementRenderInfo>& elements,
                                     float parent_opacity,
                                     int depth) {
  if (!element || !element->IsObject()) {
    return;
  }

  // Prevent stack overflow on deeply nested DOMs
  if (depth > 100) {
    LOG_DEBUG("ElementScanner", "Reached max depth (100), stopping recursion");
    return;
  }

  // Skip elements marked with data-owl-ignore (e.g., automation overlay)
  CefRefPtr<CefV8Value> getAttribute = element->GetValue("getAttribute");
  if (getAttribute && getAttribute->IsFunction()) {
    CefV8ValueList args;
    args.push_back(CefV8Value::CreateString("data-owl-ignore"));
    CefRefPtr<CefV8Value> attrValue = getAttribute->ExecuteFunctionWithContext(context, element, args);
    if (attrValue && attrValue->IsString() && attrValue->GetStringValue().ToString() == "true") {
      // Skip this element and its children
      return;
    }
  }

  // Extract info from this element with parent opacity context
  ElementRenderInfo info = ExtractElementInfo(context, element, parent_opacity);

  // Debug: Log IMG elements as soon as we encounter them
  if (info.tag == "img") {
    LOG_DEBUG("ElementScanner", "FOUND IMG at depth " + std::to_string(depth) +
             ": alt='" + info.alt + "' src='" + (info.src.length() > 40 ? info.src.substr(0, 40) : info.src) +
             "' class='" + info.className + "' visible=" + (info.visible ? "true" : "false"));
  }

  // Calculate cumulative opacity for passing to children
  float current_opacity = info.opacity * parent_opacity;

  // Track visible elements, but make exception for input/button/select/textarea which should always be tracked
  // even if they have small/zero dimensions or are hidden with CSS (e.g., checkboxes with display:none but custom styling)
  // Also always track IMG with alt attribute (needed for CAPTCHA gesture detection)
  bool is_interactive_input = (info.tag == "input" || info.tag == "button" ||
                               info.tag == "select" || info.tag == "textarea" ||
                               info.tag == "a" || info.role == "button" ||
                               info.role == "link" || info.role == "checkbox" ||
                               info.role == "radio" || info.role == "menuitem");

  // IMG elements with alt are important for CAPTCHA - track them even if hidden
  bool is_important_img = (info.tag == "img" && !info.alt.empty());

  // Dynamic element limit: allow more elements for interactive ones
  size_t current_limit = kBaseElementLimit;
  size_t interactive_count = 0;
  for (const auto& e : elements) {
    if (e.tag == "input" || e.tag == "button" || e.tag == "a" || e.tag == "select") {
      interactive_count++;
    }
  }
  // Bonus slots for interactive elements
  current_limit += std::min(interactive_count, static_cast<size_t>(kInteractiveElementBonus));

  // Check visibility with cascaded opacity
  bool visibility_ok = info.visible && (info.width > 0 && info.height > 0);

  // Apply cumulative opacity threshold - if parent chain makes element effectively invisible
  if (visibility_ok && current_opacity < kCumulativeOpacityThreshold) {
    visibility_ok = false;
    LOG_DEBUG("ElementScanner", "Element hidden by cumulative opacity: " + std::to_string(current_opacity));
  }

  // Interactive inputs and important IMGs are always tracked (even if hidden), other elements must be visible with size > 0
  bool should_track = is_interactive_input || is_important_img || visibility_ok;

  // Debug: Log IMG tracking decision
  if (info.tag == "img" && !info.alt.empty()) {
    LOG_DEBUG("ElementScanner", "IMG tracking decision: alt='" + info.alt +
             "' is_important_img=" + (is_important_img ? "true" : "false") +
             " should_track=" + (should_track ? "true" : "false") +
             " elements.size=" + std::to_string(elements.size()) +
             " limit=" + std::to_string(current_limit));
  }

  if (should_track && elements.size() < current_limit) {
    // Store cumulative opacity for downstream use
    info.opacity = current_opacity;
    elements.push_back(info);
  }

  // Check for shadow DOM - scan shadow root if present
  CefRefPtr<CefV8Value> shadowRoot = element->GetValue("shadowRoot");
  if (shadowRoot && shadowRoot->IsObject() && !shadowRoot->IsNull()) {
    LOG_DEBUG("ElementScanner", "Found shadow root, scanning...");

    // Also try to access slotted content via assignedNodes
    CefRefPtr<CefV8Value> querySelectorAll = shadowRoot->GetValue("querySelectorAll");
    if (querySelectorAll && querySelectorAll->IsFunction()) {
      CefV8ValueList slotArgs;
      slotArgs.push_back(CefV8Value::CreateString("slot"));
      CefRefPtr<CefV8Value> slots = querySelectorAll->ExecuteFunctionWithContext(context, shadowRoot, slotArgs);
      if (slots && slots->IsObject()) {
        CefRefPtr<CefV8Value> slotLength = slots->GetValue("length");
        if (slotLength && slotLength->IsInt()) {
          int slotCount = slotLength->GetIntValue();
          for (int s = 0; s < slotCount && elements.size() < current_limit; s++) {
            CefRefPtr<CefV8Value> slot = slots->GetValue(s);
            if (slot && slot->IsObject()) {
              CefRefPtr<CefV8Value> assignedNodes = slot->GetValue("assignedNodes");
              if (assignedNodes && assignedNodes->IsFunction()) {
                CefV8ValueList nodeArgs;
                CefRefPtr<CefV8Value> nodes = assignedNodes->ExecuteFunctionWithContext(context, slot, nodeArgs);
                if (nodes && nodes->IsObject()) {
                  CefRefPtr<CefV8Value> nodeLength = nodes->GetValue("length");
                  if (nodeLength && nodeLength->IsInt()) {
                    int nodeCount = nodeLength->GetIntValue();
                    for (int n = 0; n < nodeCount && elements.size() < current_limit; n++) {
                      CefRefPtr<CefV8Value> node = nodes->GetValue(n);
                      if (node && node->IsObject()) {
                        // Check if it's an element node (nodeType === 1)
                        CefRefPtr<CefV8Value> nodeType = node->GetValue("nodeType");
                        if (nodeType && nodeType->IsInt() && nodeType->GetIntValue() == 1) {
                          ScanDOMTree(context, node, elements, current_opacity, depth + 1);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    // Scan shadow root children
    CefRefPtr<CefV8Value> shadowChildren = shadowRoot->GetValue("children");
    if (shadowChildren && shadowChildren->IsObject()) {
      CefRefPtr<CefV8Value> shadowLength = shadowChildren->GetValue("length");
      if (shadowLength && shadowLength->IsInt()) {
        int shadowChildCount = shadowLength->GetIntValue();
        for (int i = 0; i < shadowChildCount && elements.size() < current_limit; i++) {
          CefRefPtr<CefV8Value> shadowChild = shadowChildren->GetValue(i);
          ScanDOMTree(context, shadowChild, elements, current_opacity, depth + 1);
        }
      }
    }
  }

  // Check for iframes - scan iframe content if accessible
  std::string tagName = info.tag;
  std::transform(tagName.begin(), tagName.end(), tagName.begin(), ::toupper);
  if (tagName == "IFRAME") {
    CefRefPtr<CefV8Value> contentDocument = element->GetValue("contentDocument");
    if (contentDocument && contentDocument->IsObject() && !contentDocument->IsNull()) {
      LOG_DEBUG("ElementScanner", "Found accessible iframe, scanning...");
      CefRefPtr<CefV8Value> iframeBody = contentDocument->GetValue("body");
      if (iframeBody && iframeBody->IsObject()) {
        // Reset opacity for iframe content (new document context)
        ScanDOMTree(context, iframeBody, elements, 1.0f, depth + 1);
      }
    }
  }

  // Recursively scan children
  CefRefPtr<CefV8Value> children = element->GetValue("children");
  if (children && children->IsObject()) {
    CefRefPtr<CefV8Value> length = children->GetValue("length");
    if (length && length->IsInt()) {
      int childCount = length->GetIntValue();

      // Use configurable limits
      if (elements.size() < current_limit && childCount < kMaxChildrenPerNode) {
        for (int i = 0; i < childCount; i++) {
          if (elements.size() >= current_limit) {
            LOG_DEBUG("ElementScanner", "Reached element limit (" + std::to_string(current_limit) + "), stopping scan");
            break;
          }
          CefRefPtr<CefV8Value> child = children->GetValue(i);
          ScanDOMTree(context, child, elements, current_opacity, depth + 1);
        }
      }
    }
  }
}

ElementRenderInfo OwlElementScanner::ExtractElementInfo(CefRefPtr<CefV8Context> context,
                                                         CefRefPtr<CefV8Value> element,
                                                         float parent_opacity) {
  ElementRenderInfo info;
  info.visible = false;
  info.x = 0;
  info.y = 0;
  info.width = 0;
  info.height = 0;
  info.z_index = 0;
  info.opacity = 1.0f;

  if (!element || !element->IsObject()) {
    return info;
  }

  // Get tag name
  CefRefPtr<CefV8Value> tagName = element->GetValue("tagName");
  if (tagName && tagName->IsString()) {
    info.tagName = tagName->GetStringValue().ToString();  // Original uppercase
    info.tag = info.tagName;
    // Convert to lowercase to match CSS selector convention
    std::transform(info.tag.begin(), info.tag.end(), info.tag.begin(), ::tolower);
  }

  // Get ID
  CefRefPtr<CefV8Value> id = element->GetValue("id");
  if (id && id->IsString()) {
    info.id = id->GetStringValue().ToString();
  }

  // Get className
  CefRefPtr<CefV8Value> className = element->GetValue("className");
  if (className && className->IsString()) {
    info.className = className->GetStringValue().ToString();
  }

  // Get bounding rect
  CefRefPtr<CefV8Value> getBoundingClientRect = element->GetValue("getBoundingClientRect");
  if (getBoundingClientRect && getBoundingClientRect->IsFunction()) {
    CefV8ValueList args;
    CefRefPtr<CefV8Value> rect = getBoundingClientRect->ExecuteFunctionWithContext(context, element, args);

    if (rect && rect->IsObject()) {
      CefRefPtr<CefV8Value> left = rect->GetValue("left");
      CefRefPtr<CefV8Value> top = rect->GetValue("top");
      CefRefPtr<CefV8Value> width = rect->GetValue("width");
      CefRefPtr<CefV8Value> height = rect->GetValue("height");

      if (left && left->IsDouble()) info.x = static_cast<int>(left->GetDoubleValue());
      if (top && top->IsDouble()) info.y = static_cast<int>(top->GetDoubleValue());
      if (width && width->IsDouble()) info.width = static_cast<int>(width->GetDoubleValue());
      if (height && height->IsDouble()) info.height = static_cast<int>(height->GetDoubleValue());

      info.visible = (info.width > 0 && info.height > 0);
    }
  }

  // Get computed styles for better visibility detection
  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<CefV8Value> window = global->GetValue("window");
  if (window && window->IsObject()) {
    CefRefPtr<CefV8Value> getComputedStyle = window->GetValue("getComputedStyle");
    if (getComputedStyle && getComputedStyle->IsFunction()) {
      CefV8ValueList styleArgs;
      styleArgs.push_back(element);
      CefRefPtr<CefV8Value> computedStyle = getComputedStyle->ExecuteFunctionWithContext(context, window, styleArgs);

      if (computedStyle && computedStyle->IsObject()) {
        // Check display property
        CefRefPtr<CefV8Value> display = computedStyle->GetValue("display");
        if (display && display->IsString()) {
          info.display = display->GetStringValue().ToString();
          if (info.display == "none") {
            info.visible = false;
          }
        }

        // Check visibility property
        CefRefPtr<CefV8Value> visibility = computedStyle->GetValue("visibility");
        if (visibility && visibility->IsString()) {
          info.visibility_css = visibility->GetStringValue().ToString();
          if (info.visibility_css == "hidden") {
            info.visible = false;
          }
        }

        // Check opacity - use element's own opacity, cascading handled by parent_opacity
        CefRefPtr<CefV8Value> opacity = computedStyle->GetValue("opacity");
        if (opacity && (opacity->IsDouble() || opacity->IsString())) {
          if (opacity->IsDouble()) {
            info.opacity = static_cast<float>(opacity->GetDoubleValue());
          } else {
            std::string opacityStr = opacity->GetStringValue().ToString();
            // Parse without exceptions (exceptions disabled in this codebase)
            if (!opacityStr.empty()) {
              char* endptr = nullptr;
              float parsed = std::strtof(opacityStr.c_str(), &endptr);
              if (endptr != opacityStr.c_str()) {
                info.opacity = parsed;
              } else {
                info.opacity = 1.0f;  // Default to visible on parse error
              }
            }
          }
          // Check element's own opacity (cascading is handled in ScanDOMTree)
          if (info.opacity < kMinVisibleOpacity) {
            info.visible = false;
          }
        }

        // Get z-index
        CefRefPtr<CefV8Value> zIndex = computedStyle->GetValue("zIndex");
        if (zIndex && (zIndex->IsInt() || zIndex->IsString())) {
          if (zIndex->IsInt()) {
            info.z_index = zIndex->GetIntValue();
          } else {
            std::string zIndexStr = zIndex->GetStringValue().ToString();
            if (zIndexStr != "auto" && !zIndexStr.empty()) {
              // Parse manually to avoid std::stoi which may throw
              bool negative = (zIndexStr[0] == '-');
              size_t start = negative ? 1 : 0;
              int result = 0;
              for (size_t i = start; i < zIndexStr.length() && std::isdigit(zIndexStr[i]); i++) {
                result = result * 10 + (zIndexStr[i] - '0');
              }
              info.z_index = negative ? -result : result;
            }
          }
        }

        // Get transform
        CefRefPtr<CefV8Value> transform = computedStyle->GetValue("transform");
        if (transform && transform->IsString()) {
          info.transform = transform->GetStringValue().ToString();
        }
      }
    }
  }

  // Build selector string (will be completed after extracting attributes)

  // Extract all semantic attributes using getAttribute
  CefRefPtr<CefV8Value> getAttribute = element->GetValue("getAttribute");
  if (getAttribute && getAttribute->IsFunction()) {
    // aria-label
    CefV8ValueList ariaLabelArgs;
    ariaLabelArgs.push_back(CefV8Value::CreateString("aria-label"));
    CefRefPtr<CefV8Value> ariaLabel = getAttribute->ExecuteFunctionWithContext(context, element, ariaLabelArgs);
    if (ariaLabel && !ariaLabel->IsNull() && !ariaLabel->IsUndefined() && ariaLabel->IsString()) {
      info.aria_label = ariaLabel->GetStringValue().ToString();
    }

    // role attribute (ARIA role or custom data-role)
    CefV8ValueList roleArgs;
    roleArgs.push_back(CefV8Value::CreateString("role"));
    CefRefPtr<CefV8Value> roleAttr = getAttribute->ExecuteFunctionWithContext(context, element, roleArgs);
    if (roleAttr && !roleAttr->IsNull() && !roleAttr->IsUndefined() && roleAttr->IsString()) {
      info.role = roleAttr->GetStringValue().ToString();
    }

    // data-role attribute (common pattern)
    if (info.role.empty()) {
      CefV8ValueList dataRoleArgs;
      dataRoleArgs.push_back(CefV8Value::CreateString("data-role"));
      CefRefPtr<CefV8Value> dataRole = getAttribute->ExecuteFunctionWithContext(context, element, dataRoleArgs);
      if (dataRole && !dataRole->IsNull() && !dataRole->IsUndefined() && dataRole->IsString()) {
        info.role = dataRole->GetStringValue().ToString();
      }
    }

    // title attribute
    CefV8ValueList titleArgs;
    titleArgs.push_back(CefV8Value::CreateString("title"));
    CefRefPtr<CefV8Value> titleAttr = getAttribute->ExecuteFunctionWithContext(context, element, titleArgs);
    if (titleAttr && !titleAttr->IsNull() && !titleAttr->IsUndefined() && titleAttr->IsString()) {
      info.title = titleAttr->GetStringValue().ToString();
    }

    // data-* attributes (collect common ones)
    std::vector<std::string> dataAttrs = {"data-test-id", "data-testid", "data-cy", "data-qa", "data-label", "data-name"};
    for (const auto& dataAttr : dataAttrs) {
      CefV8ValueList dataArgs;
      dataArgs.push_back(CefV8Value::CreateString(dataAttr));
      CefRefPtr<CefV8Value> dataVal = getAttribute->ExecuteFunctionWithContext(context, element, dataArgs);
      if (dataVal && !dataVal->IsNull() && !dataVal->IsUndefined() && dataVal->IsString()) {
        std::string dataText = dataVal->GetStringValue().ToString();
        if (!dataText.empty() && info.nearby_text.empty()) {
          info.nearby_text = dataText;  // Use data-* as nearby text hint
        }
      }
    }

    // For label elements, capture the "for" attribute
    if (info.tag == "label") {
      CefV8ValueList forArgs;
      forArgs.push_back(CefV8Value::CreateString("for"));
      CefRefPtr<CefV8Value> forAttr = getAttribute->ExecuteFunctionWithContext(context, element, forArgs);
      if (forAttr && !forAttr->IsNull() && !forAttr->IsUndefined() && forAttr->IsString()) {
        info.label_for = forAttr->GetStringValue().ToString();
      }
    }

    // For IMG elements, capture alt and src attributes
    if (info.tag == "img") {
      CefV8ValueList altArgs;
      altArgs.push_back(CefV8Value::CreateString("alt"));
      CefRefPtr<CefV8Value> altAttr = getAttribute->ExecuteFunctionWithContext(context, element, altArgs);
      if (altAttr && !altAttr->IsNull() && !altAttr->IsUndefined() && altAttr->IsString()) {
        info.alt = altAttr->GetStringValue().ToString();
      }

      CefV8ValueList srcArgs;
      srcArgs.push_back(CefV8Value::CreateString("src"));
      CefRefPtr<CefV8Value> srcAttr = getAttribute->ExecuteFunctionWithContext(context, element, srcArgs);
      if (srcAttr && !srcAttr->IsNull() && !srcAttr->IsUndefined() && srcAttr->IsString()) {
        info.src = srcAttr->GetStringValue().ToString();
      }
    }
  }

  // Get element-specific properties
  if (info.tag == "input" || info.tag == "textarea" || info.tag == "button") {
    // Get name attribute
    CefRefPtr<CefV8Value> nameVal = element->GetValue("name");
    if (nameVal && nameVal->IsString()) {
      info.name = nameVal->GetStringValue().ToString();
      if (!info.name.empty() && info.tag == "input") {
        info.selector = info.tag + "[name=\"" + info.name + "\"]";
      }
    }

    // Get type attribute (for input elements)
    CefRefPtr<CefV8Value> typeVal = element->GetValue("type");
    if (typeVal && typeVal->IsString()) {
      info.type = typeVal->GetStringValue().ToString();
    }

    // Get placeholder
    CefRefPtr<CefV8Value> placeholderVal = element->GetValue("placeholder");
    if (placeholderVal && placeholderVal->IsString()) {
      info.placeholder = placeholderVal->GetStringValue().ToString();
    }

    // Get value
    CefRefPtr<CefV8Value> valueVal = element->GetValue("value");
    if (valueVal && valueVal->IsString()) {
      info.value = valueVal->GetStringValue().ToString();
    }
  }

  // Extract nearby text from labels (important for semantic matching)
  if (info.tag == "input" || info.tag == "textarea" || info.tag == "select") {
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    CefRefPtr<CefV8Value> document = global->GetValue("document");

    // Strategy 1: Try to find associated label by 'for' attribute
    if (!info.id.empty() && document && document->IsObject()) {
      CefRefPtr<CefV8Value> querySelector = document->GetValue("querySelector");
      if (querySelector && querySelector->IsFunction()) {
        std::string labelSelector = "label[for=\"" + info.id + "\"]";
        CefV8ValueList labelArgs;
        labelArgs.push_back(CefV8Value::CreateString(labelSelector));
        CefRefPtr<CefV8Value> label = querySelector->ExecuteFunctionWithContext(context, document, labelArgs);
        if (label && !label->IsNull() && !label->IsUndefined()) {
          CefRefPtr<CefV8Value> labelText = label->GetValue("textContent");
          if (labelText && labelText->IsString()) {
            std::string text = labelText->GetStringValue().ToString();
            // Trim whitespace
            size_t start = text.find_first_not_of(" \t\n\r");
            size_t end = text.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
              info.nearby_text = text.substr(start, end - start + 1);
            }
          }
        }
      }
    }

    // Strategy 2: Check if element is wrapped inside a LABEL (common for checkboxes/radios)
    // Walk up parent chain to find any ancestor LABEL
    // Note: info.tag is lowercase (converted earlier), parentTag from DOM is uppercase
    if (info.nearby_text.empty() && info.tag == "input") {
      CefRefPtr<CefV8Value> currentNode = element->GetValue("parentNode");
      int maxDepth = 5;  // Don't walk too far up
      while (currentNode && currentNode->IsObject() && maxDepth > 0) {
        CefRefPtr<CefV8Value> parentTag = currentNode->GetValue("tagName");
        if (parentTag && parentTag->IsString()) {
          std::string tagName = parentTag->GetStringValue().ToString();
          if (tagName == "LABEL") {
            CefRefPtr<CefV8Value> labelText = currentNode->GetValue("textContent");
            if (labelText && labelText->IsString()) {
              std::string text = labelText->GetStringValue().ToString();
              // Trim whitespace
              size_t start = text.find_first_not_of(" \t\n\r");
              size_t end = text.find_last_not_of(" \t\n\r");
              if (start != std::string::npos && end != std::string::npos) {
                info.nearby_text = text.substr(start, end - start + 1);
              }
            }
            break;  // Found a label, stop searching
          }
          // Stop if we hit BODY or FORM - but allow traversing through DIVs
          if (tagName == "BODY" || tagName == "FORM") {
            break;
          }
        }
        currentNode = currentNode->GetValue("parentNode");
        maxDepth--;
      }
    }

    // Strategy 3: Check previous sibling for LABEL
    if (info.nearby_text.empty()) {
      CefRefPtr<CefV8Value> prevSibling = element->GetValue("previousElementSibling");
      if (prevSibling && prevSibling->IsObject()) {
        CefRefPtr<CefV8Value> prevTag = prevSibling->GetValue("tagName");
        if (prevTag && prevTag->IsString() && prevTag->GetStringValue().ToString() == "LABEL") {
          CefRefPtr<CefV8Value> labelText = prevSibling->GetValue("textContent");
          if (labelText && labelText->IsString()) {
            std::string text = labelText->GetStringValue().ToString();
            // Trim whitespace
            size_t start = text.find_first_not_of(" \t\n\r");
            size_t end = text.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
              info.nearby_text = text.substr(start, end - start + 1);
            }
          }
        }
      }
    }

    // Strategy 4: Check next sibling for LABEL or text-containing SPAN
    if (info.nearby_text.empty()) {
      CefRefPtr<CefV8Value> nextSibling = element->GetValue("nextElementSibling");
      if (nextSibling && nextSibling->IsObject()) {
        CefRefPtr<CefV8Value> nextTag = nextSibling->GetValue("tagName");
        if (nextTag && nextTag->IsString()) {
          std::string tagName = nextTag->GetStringValue().ToString();
          if (tagName == "LABEL" || tagName == "SPAN") {
            CefRefPtr<CefV8Value> siblingText = nextSibling->GetValue("textContent");
            if (siblingText && siblingText->IsString()) {
              std::string text = siblingText->GetStringValue().ToString();
              // Trim whitespace
              size_t start = text.find_first_not_of(" \t\n\r");
              size_t end = text.find_last_not_of(" \t\n\r");
              if (start != std::string::npos && end != std::string::npos) {
                info.nearby_text = text.substr(start, end - start + 1);
              }
            }
          }
        }
      }
    }
  }

  // Extract text content for ALL elements
  CefRefPtr<CefV8Value> textContent = element->GetValue("textContent");
  if (textContent && textContent->IsString()) {
    std::string text = textContent->GetStringValue().ToString();

    // Trim whitespace
    size_t start = text.find_first_not_of(" \t\n\r");
    size_t end = text.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
      text = text.substr(start, end - start + 1);
      if (text.length() > 100) {
        text = text.substr(0, 100);
      }
      info.text = text;
    }
  }

  // Build comprehensive selector string now that all attributes are extracted
  // Priority order: ID > data-testid > name > class > tag with important attributes > tag only
  if (!info.id.empty()) {
    info.selector = "#" + info.id;
  } else {
    // Start with tag
    info.selector = info.tag;

    // Check for data-testid in nearby_text (from earlier extraction)
    bool hasDataTestId = false;
    if (!info.nearby_text.empty() && getAttribute && getAttribute->IsFunction()) {
      CefV8ValueList testidArgs;
      testidArgs.push_back(CefV8Value::CreateString("data-testid"));
      CefRefPtr<CefV8Value> testidVal = getAttribute->ExecuteFunctionWithContext(context, element, testidArgs);
      if (testidVal && !testidVal->IsNull() && !testidVal->IsUndefined() && testidVal->IsString()) {
        std::string testid = testidVal->GetStringValue().ToString();
        if (!testid.empty()) {
          info.selector = "[data-testid=\"" + testid + "\"]";
          hasDataTestId = true;
        }
      }
    }

    // Add name attribute if present (high priority)
    if (!hasDataTestId && !info.name.empty()) {
      info.selector += "[name=\"" + info.name + "\"]";
    } else if (!hasDataTestId) {
      // Build selector with important attributes
      std::vector<std::string> attrSelectors;

      // Add aria-label if present
      if (!info.aria_label.empty()) {
        attrSelectors.push_back("[aria-label=\"" + info.aria_label + "\"]");
      }

      // Add role if present
      if (!info.role.empty()) {
        attrSelectors.push_back("[role=\"" + info.role + "\"]");
      }

      // Add type if present (for inputs)
      if (!info.type.empty()) {
        attrSelectors.push_back("[type=\"" + info.type + "\"]");
      }

      // Add title if present and no other attrs
      if (attrSelectors.empty() && !info.title.empty()) {
        attrSelectors.push_back("[title=\"" + info.title + "\"]");
      }

      // If we have attribute selectors, add them to tag
      if (!attrSelectors.empty()) {
        for (const auto& attr : attrSelectors) {
          info.selector += attr;
        }
      } else if (!info.className.empty()) {
        // Fallback to class if no attributes
        std::string firstClass = info.className;
        size_t space = firstClass.find(' ');
        if (space != std::string::npos) {
          firstClass = firstClass.substr(0, space);
        }
        info.selector = info.tag + "." + firstClass;
      }
      // Otherwise keep just tag name
    }
  }

  // Store position in selector as metadata for click verification
  // Format: "TAG.class@x,y" or "TAG[attr]@x,y"
  info.selector += "@" + std::to_string(info.x) + "," + std::to_string(info.y);

  return info;
}

VisibilityInfo OwlElementScanner::AnalyzeVisibility(CefRefPtr<CefV8Context> context,
                                                     CefRefPtr<CefV8Value> element,
                                                     const ElementRenderInfo& info,
                                                     float parent_opacity) {
  VisibilityInfo result;
  result.visible = info.visible;
  result.cumulative_opacity = info.opacity * parent_opacity;
  result.clipped_by_overflow = false;

  // Check if effectively invisible due to cumulative opacity
  if (result.cumulative_opacity < kCumulativeOpacityThreshold) {
    result.visible = false;
    result.hidden_reason = "cumulative_opacity=" + std::to_string(result.cumulative_opacity);
    return result;
  }

  // Check if clipped by ancestor overflow
  if (info.visible && IsClippedByOverflow(context, element, info)) {
    result.clipped_by_overflow = true;
    result.visible = false;
    result.hidden_reason = "clipped_by_overflow";
    return result;
  }

  if (!result.visible) {
    // Determine reason
    if (info.display == "none") {
      result.hidden_reason = "display:none";
    } else if (info.visibility_css == "hidden") {
      result.hidden_reason = "visibility:hidden";
    } else if (info.opacity < kMinVisibleOpacity) {
      result.hidden_reason = "opacity=" + std::to_string(info.opacity);
    } else if (info.width <= 0 || info.height <= 0) {
      result.hidden_reason = "zero_size";
    }
  }

  return result;
}

bool OwlElementScanner::IsClippedByOverflow(CefRefPtr<CefV8Context> context,
                                             CefRefPtr<CefV8Value> element,
                                             const ElementRenderInfo& info) {
  // Check if element is completely outside an ancestor with overflow:hidden/clip
  // This is an expensive check, so we limit to a few ancestors

  if (info.width <= 0 || info.height <= 0) {
    return false;  // Already invisible, skip check
  }

  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<CefV8Value> window = global->GetValue("window");
  if (!window || !window->IsObject()) {
    return false;
  }

  CefRefPtr<CefV8Value> getComputedStyle = window->GetValue("getComputedStyle");
  if (!getComputedStyle || !getComputedStyle->IsFunction()) {
    return false;
  }

  // Walk up parent chain (limit to 10 ancestors for performance)
  CefRefPtr<CefV8Value> current = element->GetValue("parentElement");
  int depth = 0;
  const int maxDepth = 10;

  while (current && current->IsObject() && !current->IsNull() && depth < maxDepth) {
    // Get computed style of parent
    CefV8ValueList styleArgs;
    styleArgs.push_back(current);
    CefRefPtr<CefV8Value> style = getComputedStyle->ExecuteFunctionWithContext(context, window, styleArgs);

    if (style && style->IsObject()) {
      // Check overflow property
      CefRefPtr<CefV8Value> overflow = style->GetValue("overflow");
      CefRefPtr<CefV8Value> overflowX = style->GetValue("overflowX");
      CefRefPtr<CefV8Value> overflowY = style->GetValue("overflowY");

      std::string overflowStr, overflowXStr, overflowYStr;
      if (overflow && overflow->IsString()) overflowStr = overflow->GetStringValue().ToString();
      if (overflowX && overflowX->IsString()) overflowXStr = overflowX->GetStringValue().ToString();
      if (overflowY && overflowY->IsString()) overflowYStr = overflowY->GetStringValue().ToString();

      bool hasClippingOverflow = (overflowStr == "hidden" || overflowStr == "clip" ||
                                  overflowStr == "scroll" || overflowStr == "auto" ||
                                  overflowXStr == "hidden" || overflowXStr == "clip" ||
                                  overflowYStr == "hidden" || overflowYStr == "clip");

      if (hasClippingOverflow) {
        // Get parent bounds
        CefRefPtr<CefV8Value> getBoundingClientRect = current->GetValue("getBoundingClientRect");
        if (getBoundingClientRect && getBoundingClientRect->IsFunction()) {
          CefV8ValueList rectArgs;
          CefRefPtr<CefV8Value> rect = getBoundingClientRect->ExecuteFunctionWithContext(context, current, rectArgs);

          if (rect && rect->IsObject()) {
            CefRefPtr<CefV8Value> left = rect->GetValue("left");
            CefRefPtr<CefV8Value> top = rect->GetValue("top");
            CefRefPtr<CefV8Value> right = rect->GetValue("right");
            CefRefPtr<CefV8Value> bottom = rect->GetValue("bottom");

            if (left && top && right && bottom &&
                left->IsDouble() && top->IsDouble() && right->IsDouble() && bottom->IsDouble()) {
              int parentLeft = static_cast<int>(left->GetDoubleValue());
              int parentTop = static_cast<int>(top->GetDoubleValue());
              int parentRight = static_cast<int>(right->GetDoubleValue());
              int parentBottom = static_cast<int>(bottom->GetDoubleValue());

              // Check if element is completely outside parent bounds
              int elemRight = info.x + info.width;
              int elemBottom = info.y + info.height;

              if (elemRight <= parentLeft || info.x >= parentRight ||
                  elemBottom <= parentTop || info.y >= parentBottom) {
                return true;  // Completely clipped
              }
            }
          }
        }
      }
    }

    current = current->GetValue("parentElement");
    depth++;
  }

  return false;
}

void OwlElementScanner::SendElementsToBrowserProcess(CefRefPtr<CefBrowser> browser,
                                                       CefRefPtr<CefFrame> frame,
                                                       const std::string& context_id,
                                                       const std::vector<ElementRenderInfo>& elements) {
  // Create process message
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("element_scan_result");
  CefRefPtr<CefListValue> args = message->GetArgumentList();

  // Pack context_id and elements into message
  args->SetString(0, context_id);
  args->SetInt(1, elements.size());

  for (size_t i = 0; i < elements.size(); i++) {
    const ElementRenderInfo& elem = elements[i];

    // Create a dictionary for this element
    CefRefPtr<CefDictionaryValue> dict = CefDictionaryValue::Create();
    dict->SetString("selector", elem.selector);
    dict->SetString("tag", elem.tag);
    dict->SetString("id", elem.id);
    dict->SetString("className", elem.className);
    dict->SetString("text", elem.text);
    dict->SetString("aria_label", elem.aria_label);
    dict->SetInt("x", elem.x);
    dict->SetInt("y", elem.y);
    dict->SetInt("width", elem.width);
    dict->SetInt("height", elem.height);
    dict->SetBool("visible", elem.visible);

    // Enhanced attributes
    dict->SetString("role", elem.role);
    dict->SetString("placeholder", elem.placeholder);
    dict->SetString("value", elem.value);
    dict->SetString("type", elem.type);
    dict->SetString("title", elem.title);
    dict->SetString("name", elem.name);
    dict->SetString("nearby_text", elem.nearby_text);
    dict->SetString("label_for", elem.label_for);
    dict->SetString("alt", elem.alt);
    dict->SetString("src", elem.src);

    // Enhanced position/visibility info
    dict->SetInt("z_index", elem.z_index);
    dict->SetString("transform", elem.transform);
    dict->SetDouble("opacity", elem.opacity);
    dict->SetString("display", elem.display);
    dict->SetString("visibility_css", elem.visibility_css);

    args->SetDictionary(i + 2, dict);
  }

  // Send to browser process
  frame->SendProcessMessage(PID_BROWSER, message);
  LOG_DEBUG("ElementScanner", "Sent " + std::to_string(elements.size()) + " elements to browser");
}
