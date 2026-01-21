#include "owl_element_type_scorer.h"
#include "owl_text_similarity_scorer.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <sstream>

ElementTypeScorer* ElementTypeScorer::instance_ = nullptr;

ElementTypeScorer* ElementTypeScorer::GetInstance() {
  if (!instance_) {
    instance_ = new ElementTypeScorer();
  }
  return instance_;
}

ElementTypeScorer::ElementTypeScorer() {
  InitializeElementPriorities();
  InitializeInputTypes();
  InitializeARIARoles();
}

void ElementTypeScorer::InitializeElementPriorities() {
  // Higher priority = more likely to be the target of user interaction
  // Scale: 0.0 (never interact) to 1.0 (primary interaction target)

  // Primary interactive elements
  elementPriorities_["button"] = 1.0f;
  elementPriorities_["a"] = 0.95f;
  elementPriorities_["input"] = 0.95f;
  elementPriorities_["select"] = 0.90f;
  elementPriorities_["textarea"] = 0.90f;

  // Secondary interactive elements
  elementPriorities_["label"] = 0.75f;  // Often clickable for associated input
  elementPriorities_["option"] = 0.70f;
  elementPriorities_["summary"] = 0.65f;  // Details/summary pattern
  elementPriorities_["details"] = 0.60f;

  // Potentially interactive (depending on JS)
  elementPriorities_["div"] = 0.40f;
  elementPriorities_["span"] = 0.35f;
  elementPriorities_["li"] = 0.50f;
  elementPriorities_["tr"] = 0.45f;
  elementPriorities_["td"] = 0.40f;

  // Content elements (rarely interactive)
  elementPriorities_["p"] = 0.20f;
  elementPriorities_["h1"] = 0.25f;
  elementPriorities_["h2"] = 0.25f;
  elementPriorities_["h3"] = 0.25f;
  elementPriorities_["img"] = 0.50f;  // Often wrapped in links

  // Container elements (almost never directly interactive)
  elementPriorities_["form"] = 0.15f;
  elementPriorities_["section"] = 0.10f;
  elementPriorities_["article"] = 0.10f;
  elementPriorities_["nav"] = 0.15f;
  elementPriorities_["header"] = 0.10f;
  elementPriorities_["footer"] = 0.10f;

  // Interactive tags set
  interactiveTags_ = {
    "button", "a", "input", "select", "textarea",
    "label", "option", "summary", "details"
  };

  // Form control tags
  formControlTags_ = {
    "input", "select", "textarea", "button", "option", "optgroup"
  };
}

void ElementTypeScorer::InitializeInputTypes() {
  // Map input types to related keywords
  inputTypeKeywords_["text"] = {"text", "name", "username", "user", "field", "input"};
  inputTypeKeywords_["email"] = {"email", "mail", "e-mail"};
  inputTypeKeywords_["password"] = {"password", "pass", "pwd", "secret", "pin"};
  inputTypeKeywords_["search"] = {"search", "find", "query", "lookup"};
  inputTypeKeywords_["tel"] = {"phone", "telephone", "mobile", "cell", "tel"};
  inputTypeKeywords_["number"] = {"number", "qty", "quantity", "amount", "count"};
  inputTypeKeywords_["url"] = {"url", "website", "link", "web", "http"};
  inputTypeKeywords_["date"] = {"date", "birthday", "dob", "day"};
  inputTypeKeywords_["datetime-local"] = {"datetime", "when", "schedule"};
  inputTypeKeywords_["time"] = {"time", "hour", "minute"};
  inputTypeKeywords_["checkbox"] = {"checkbox", "check", "agree", "accept", "terms", "tick", "toggle"};
  inputTypeKeywords_["radio"] = {"radio", "option", "choice", "select"};
  inputTypeKeywords_["file"] = {"file", "upload", "attach", "document", "browse"};
  inputTypeKeywords_["submit"] = {"submit", "send", "go", "done", "save", "confirm"};
  inputTypeKeywords_["reset"] = {"reset", "clear", "undo"};
  inputTypeKeywords_["button"] = {"button", "click", "action"};
  inputTypeKeywords_["hidden"] = {};  // Never matched
  inputTypeKeywords_["range"] = {"slider", "range", "volume", "progress"};
  inputTypeKeywords_["color"] = {"color", "colour", "picker"};
}

void ElementTypeScorer::InitializeARIARoles() {
  // Map ARIA roles to expected behaviors
  ariaRoleBehaviors_["button"] = {"click", "press", "activate", "button"};
  ariaRoleBehaviors_["link"] = {"click", "navigate", "link", "go"};
  ariaRoleBehaviors_["textbox"] = {"type", "enter", "input", "text", "field"};
  ariaRoleBehaviors_["searchbox"] = {"search", "find", "query", "type"};
  ariaRoleBehaviors_["checkbox"] = {"check", "toggle", "tick", "select"};
  ariaRoleBehaviors_["radio"] = {"select", "choose", "option"};
  ariaRoleBehaviors_["combobox"] = {"select", "dropdown", "choose", "pick"};
  ariaRoleBehaviors_["listbox"] = {"select", "choose", "list", "pick"};
  ariaRoleBehaviors_["menu"] = {"open", "select", "menu", "navigate"};
  ariaRoleBehaviors_["menuitem"] = {"click", "select", "choose"};
  ariaRoleBehaviors_["tab"] = {"click", "select", "switch", "tab"};
  ariaRoleBehaviors_["tabpanel"] = {"view", "content", "panel"};
  ariaRoleBehaviors_["dialog"] = {"close", "dismiss", "modal"};
  ariaRoleBehaviors_["alertdialog"] = {"confirm", "dismiss", "alert"};
  ariaRoleBehaviors_["slider"] = {"slide", "adjust", "range", "value"};
  ariaRoleBehaviors_["switch"] = {"toggle", "switch", "on", "off"};
  ariaRoleBehaviors_["option"] = {"select", "choose", "option"};
  ariaRoleBehaviors_["progressbar"] = {"progress", "loading", "status"};
  ariaRoleBehaviors_["spinbutton"] = {"increment", "decrement", "number"};
}

std::string ElementTypeScorer::NormalizeTag(const std::string& tag) {
  std::string normalized;
  for (char c : tag) {
    normalized += std::tolower(static_cast<unsigned char>(c));
  }
  return normalized;
}

std::string ElementTypeScorer::ExtractTypeHint(const std::string& query) {
  std::string normalized;
  for (char c : query) {
    normalized += std::tolower(static_cast<unsigned char>(c));
  }

  // Check for explicit element type mentions
  std::vector<std::pair<std::string, std::string>> typePatterns = {
    {"button", "button"},
    {"btn", "button"},
    {"link", "a"},
    {"anchor", "a"},
    {"input", "input"},
    {"textbox", "input"},
    {"text box", "input"},
    {"text field", "input"},
    {"field", "input"},
    {"box", "input"},
    {"checkbox", "checkbox"},
    {"check box", "checkbox"},
    {"radio", "radio"},
    {"dropdown", "select"},
    {"select", "select"},
    {"picker", "select"},
    {"combobox", "select"},
    {"textarea", "textarea"},
    {"text area", "textarea"},
    {"multiline", "textarea"}
  };

  for (const auto& [pattern, type] : typePatterns) {
    if (normalized.find(pattern) != std::string::npos) {
      return type;
    }
  }

  return "";  // No explicit type hint
}

std::string ElementTypeScorer::GetSemanticType(const ElementSemantics& elem) {
  std::string tag = NormalizeTag(elem.tag);

  // For input elements, include the type
  if (tag == "input" && !elem.type.empty()) {
    std::string type = NormalizeTag(elem.type);

    // Map to semantic categories
    if (type == "submit" || type == "button") {
      return "button";
    }
    if (type == "checkbox") {
      return "checkbox";
    }
    if (type == "radio") {
      return "radio";
    }
    if (type == "file") {
      return "file_input";
    }
    if (type == "hidden") {
      return "hidden";
    }

    return "input_" + type;
  }

  return tag;
}

bool ElementTypeScorer::IsFormControl(const ElementSemantics& elem) {
  std::string tag = NormalizeTag(elem.tag);
  return formControlTags_.count(tag) > 0;
}

bool ElementTypeScorer::IsCustomComponent(const ElementSemantics& elem) {
  std::string tag = NormalizeTag(elem.tag);

  // Web components typically have hyphens in tag name
  if (tag.find('-') != std::string::npos) {
    return true;
  }

  // Check for custom element patterns
  // React/Vue often use data-* attributes
  if (!elem.id.empty()) {
    std::string id = NormalizeTag(elem.id);
    if (id.find("react") != std::string::npos ||
        id.find("vue") != std::string::npos ||
        id.find("__") != std::string::npos) {
      return true;
    }
  }

  return false;
}

float ElementTypeScorer::GetInteractivityScore(const ElementSemantics& elem) {
  std::string tag = NormalizeTag(elem.tag);

  // Base priority from tag
  float basePriority = 0.3f;
  auto it = elementPriorities_.find(tag);
  if (it != elementPriorities_.end()) {
    basePriority = it->second;
  }

  // Boost for interactive attributes
  if (!elem.aria_label.empty()) {
    basePriority = std::min(1.0f, basePriority + 0.1f);
  }

  // Boost for role attribute indicating interactivity
  if (!elem.inferred_role.empty()) {
    std::string role = NormalizeTag(elem.inferred_role);
    if (role.find("button") != std::string::npos ||
        role.find("link") != std::string::npos ||
        role.find("input") != std::string::npos) {
      basePriority = std::min(1.0f, basePriority + 0.15f);
    }
  }

  // Boost for clickable-looking elements (have onclick-like behavior)
  // We infer this from common CSS classes
  if (!elem.selector.empty()) {
    std::string selector = NormalizeTag(elem.selector);
    if (selector.find("clickable") != std::string::npos ||
        selector.find("btn") != std::string::npos ||
        selector.find("button") != std::string::npos ||
        selector.find("link") != std::string::npos) {
      basePriority = std::min(1.0f, basePriority + 0.2f);
    }
  }

  // Penalty for hidden elements
  if (!elem.visible) {
    basePriority *= 0.2f;
  }

  // Penalty for zero-size elements
  if (elem.width <= 0 || elem.height <= 0) {
    basePriority *= 0.1f;
  }

  return basePriority;
}

float ElementTypeScorer::GetInteractivePriority(const ElementSemantics& elem) {
  std::string tag = NormalizeTag(elem.tag);

  // Interactive elements get base priority
  if (interactiveTags_.count(tag)) {
    return 1.0f;
  }

  // Check for ARIA role indicating interactivity
  if (!elem.inferred_role.empty()) {
    std::string role = NormalizeTag(elem.inferred_role);
    for (const auto& [ariaRole, _] : ariaRoleBehaviors_) {
      if (role.find(ariaRole) != std::string::npos) {
        return 0.9f;
      }
    }
  }

  // Custom components might be interactive
  if (IsCustomComponent(elem)) {
    return 0.6f;
  }

  // Default low priority for non-interactive elements
  return 0.3f;
}

float ElementTypeScorer::ScoreTypeMatch(const ElementSemantics& elem, const std::string& query) {
  std::string typeHint = ExtractTypeHint(query);
  std::string elemType = GetSemanticType(elem);
  std::string tag = NormalizeTag(elem.tag);

  // Perfect match with explicit type hint
  if (!typeHint.empty()) {
    if (typeHint == tag || typeHint == elemType) {
      return 1.0f;
    }

    // Checkbox special case: match input type=checkbox
    if (typeHint == "checkbox") {
      if (tag == "input" && NormalizeTag(elem.type) == "checkbox") {
        return 1.0f;
      }
      // Labels for checkboxes also match
      if (tag == "label" && !elem.label_for.empty()) {
        return 0.85f;
      }
    }

    // Radio special case
    if (typeHint == "radio") {
      if (tag == "input" && NormalizeTag(elem.type) == "radio") {
        return 1.0f;
      }
    }

    // Partial match (e.g., button hint matches input[type=submit])
    if (typeHint == "button") {
      if (tag == "button") return 1.0f;
      if (tag == "input" && (elem.type == "submit" || elem.type == "button")) return 0.95f;
      if (tag == "a") return 0.7f;  // Links can be styled as buttons
    }

    if (typeHint == "input" || typeHint == "a") {
      if (tag == typeHint) return 1.0f;
    }
  }

  // No explicit type hint - use input type keyword matching
  if (!elem.type.empty()) {
    std::string inputType = NormalizeTag(elem.type);
    auto it = inputTypeKeywords_.find(inputType);
    if (it != inputTypeKeywords_.end()) {
      std::string queryLower;
      for (char c : query) {
        queryLower += std::tolower(static_cast<unsigned char>(c));
      }

      for (const auto& keyword : it->second) {
        if (queryLower.find(keyword) != std::string::npos) {
          return 0.9f;  // Strong match via input type keywords
        }
      }
    }
  }

  // Fallback: use element priority as base score
  return GetInteractivityScore(elem) * 0.5f;
}

float ElementTypeScorer::ScoreARIARoleMatch(const ElementSemantics& elem, const std::string& query) {
  if (elem.inferred_role.empty()) {
    return 0.5f;  // Neutral score for elements without role
  }

  std::string role = NormalizeTag(elem.inferred_role);
  std::string queryLower;
  for (char c : query) {
    queryLower += std::tolower(static_cast<unsigned char>(c));
  }

  // Check if role matches any ARIA role behaviors
  for (const auto& [ariaRole, behaviors] : ariaRoleBehaviors_) {
    if (role.find(ariaRole) != std::string::npos) {
      // Check if query mentions any behavior associated with this role
      for (const auto& behavior : behaviors) {
        if (queryLower.find(behavior) != std::string::npos) {
          return 1.0f;  // Strong match
        }
      }
      return 0.7f;  // Role matches but behavior doesn't
    }
  }

  return 0.4f;  // Unknown role
}

float ElementTypeScorer::Score(const ElementSemantics& elem, const std::string& query) {
  if (query.empty()) {
    return 0.0f;
  }

  // Calculate component scores
  float typeMatch = ScoreTypeMatch(elem, query);
  float interactivity = GetInteractivityScore(elem);
  float ariaRole = ScoreARIARoleMatch(elem, query);

  // Specificity score: how specific is this element?
  // Elements with more semantic info are preferred
  float specificity = 0.3f;
  if (!elem.aria_label.empty()) specificity += 0.2f;
  if (!elem.placeholder.empty()) specificity += 0.15f;
  if (!elem.title.empty()) specificity += 0.1f;
  if (!elem.name.empty()) specificity += 0.1f;
  if (!elem.id.empty()) specificity += 0.15f;
  specificity = std::min(1.0f, specificity);

  // Weighted combination
  float score = typeMatch * kTypeMatchWeight +
                interactivity * kInteractivityWeight +
                ariaRole * kARIARoleWeight +
                specificity * kSpecificityWeight;

  // Bonus for form controls when query is form-related
  std::string queryLower;
  for (char c : query) {
    queryLower += std::tolower(static_cast<unsigned char>(c));
  }

  if (IsFormControl(elem)) {
    bool formQuery = (queryLower.find("input") != std::string::npos ||
                      queryLower.find("field") != std::string::npos ||
                      queryLower.find("enter") != std::string::npos ||
                      queryLower.find("type") != std::string::npos ||
                      queryLower.find("fill") != std::string::npos);
    if (formQuery) {
      score = std::min(1.0f, score + 0.1f);
    }
  }

  // Penalty for hidden elements
  if (!elem.visible) {
    score *= 0.3f;
  }

  return score;
}
