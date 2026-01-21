#include "owl_contextual_relevance_scorer.h"
#include "owl_text_similarity_scorer.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <sstream>

ContextualRelevanceScorer* ContextualRelevanceScorer::instance_ = nullptr;

ContextualRelevanceScorer* ContextualRelevanceScorer::GetInstance() {
  if (!instance_) {
    instance_ = new ContextualRelevanceScorer();
  }
  return instance_;
}

ContextualRelevanceScorer::ContextualRelevanceScorer() {
  InitializeSynonymDatabase();
  InitializeActionVerbDatabase();
  InitializeDomainVocabulary();
}

void ContextualRelevanceScorer::InitializeSynonymDatabase() {
  // UI Actions
  synonyms_["click"] = {"tap", "press", "select", "hit", "touch", "activate"};
  synonyms_["tap"] = {"click", "press", "touch"};
  synonyms_["type"] = {"enter", "input", "write", "fill", "key"};
  synonyms_["submit"] = {"send", "go", "apply", "confirm", "ok", "done", "save"};
  synonyms_["search"] = {"find", "query", "lookup", "seek", "look"};
  synonyms_["cancel"] = {"close", "dismiss", "exit", "abort", "back", "no"};
  synonyms_["confirm"] = {"ok", "yes", "accept", "agree", "proceed"};

  // Authentication
  synonyms_["login"] = {"signin", "sign-in", "log-in", "authenticate", "signon"};
  synonyms_["logout"] = {"signout", "sign-out", "log-out", "exit"};
  synonyms_["register"] = {"signup", "sign-up", "join", "create-account", "enroll"};
  synonyms_["password"] = {"pass", "pwd", "passcode", "secret", "pin"};
  synonyms_["username"] = {"user", "userid", "login", "account", "handle"};
  synonyms_["email"] = {"mail", "e-mail", "emailaddress"};

  // Form Elements
  synonyms_["button"] = {"btn", "control", "action"};
  synonyms_["input"] = {"field", "textbox", "box", "entry"};
  synonyms_["checkbox"] = {"check", "tick", "toggle", "option"};
  synonyms_["dropdown"] = {"select", "picker", "combo", "list", "menu"};
  synonyms_["radio"] = {"option", "choice", "selection"};
  synonyms_["textarea"] = {"textfield", "multiline", "box", "area"};
  synonyms_["link"] = {"anchor", "href", "url", "navigation"};

  // Navigation
  synonyms_["next"] = {"continue", "forward", "proceed", "advance", "go"};
  synonyms_["previous"] = {"back", "prev", "return", "backward"};
  synonyms_["home"] = {"main", "dashboard", "start", "landing"};
  synonyms_["menu"] = {"nav", "navigation", "hamburger", "sidebar"};
  synonyms_["settings"] = {"preferences", "options", "config", "gear"};

  // Common Fields
  synonyms_["phone"] = {"telephone", "mobile", "cell", "tel", "number"};
  synonyms_["address"] = {"location", "addr", "street", "place"};
  synonyms_["name"] = {"fullname", "firstname", "lastname", "title"};
  synonyms_["date"] = {"calendar", "day", "time", "datetime"};
  synonyms_["file"] = {"upload", "attachment", "document", "browse"};

  // E-commerce
  synonyms_["cart"] = {"basket", "bag", "checkout"};
  synonyms_["buy"] = {"purchase", "order", "add-to-cart", "checkout"};
  synonyms_["price"] = {"cost", "amount", "total", "fee"};
  synonyms_["quantity"] = {"qty", "amount", "count", "number"};

  // Social
  synonyms_["like"] = {"love", "heart", "favorite", "upvote"};
  synonyms_["share"] = {"post", "send", "repost", "forward"};
  synonyms_["comment"] = {"reply", "respond", "message", "feedback"};
  synonyms_["follow"] = {"subscribe", "connect", "add"};
}

void ContextualRelevanceScorer::InitializeActionVerbDatabase() {
  // Map action verbs to expected element types
  actionToElements_["click"] = {"button", "a", "div", "span", "input"};
  actionToElements_["tap"] = {"button", "a", "div", "span"};
  actionToElements_["type"] = {"input", "textarea"};
  actionToElements_["enter"] = {"input", "textarea"};
  actionToElements_["fill"] = {"input", "textarea"};
  actionToElements_["select"] = {"select", "input", "option", "li"};
  actionToElements_["choose"] = {"select", "input", "option", "radio"};
  actionToElements_["check"] = {"input", "label"};  // checkbox
  actionToElements_["toggle"] = {"input", "button", "label"};
  actionToElements_["submit"] = {"button", "input"};  // type=submit
  actionToElements_["search"] = {"input", "button"};
  actionToElements_["upload"] = {"input"};  // type=file
  actionToElements_["download"] = {"a", "button"};
  actionToElements_["scroll"] = {"div", "section", "body"};
  actionToElements_["hover"] = {"button", "a", "div", "span"};
}

void ContextualRelevanceScorer::InitializeDomainVocabulary() {
  // Authentication domain
  domainClusters_["auth"] = {
    "login", "signin", "signup", "register", "password", "username",
    "email", "forgot", "reset", "remember", "2fa", "mfa", "otp",
    "verification", "authenticate", "credentials", "account"
  };

  // E-commerce domain
  domainClusters_["ecommerce"] = {
    "cart", "checkout", "buy", "purchase", "order", "price", "quantity",
    "shipping", "payment", "coupon", "discount", "product", "item",
    "add", "remove", "wishlist", "review", "rating"
  };

  // Search domain
  domainClusters_["search"] = {
    "search", "find", "query", "filter", "sort", "results", "advanced",
    "keyword", "tag", "category", "browse", "explore"
  };

  // Navigation domain
  domainClusters_["navigation"] = {
    "home", "menu", "nav", "back", "forward", "next", "previous",
    "page", "section", "tab", "breadcrumb", "link", "footer", "header"
  };

  // Form domain
  domainClusters_["form"] = {
    "submit", "cancel", "save", "reset", "clear", "apply", "confirm",
    "field", "input", "required", "optional", "validate", "error"
  };

  // Social domain
  domainClusters_["social"] = {
    "like", "share", "comment", "follow", "subscribe", "post", "feed",
    "profile", "notification", "message", "friend", "connect"
  };

  // Media domain
  domainClusters_["media"] = {
    "play", "pause", "stop", "volume", "mute", "fullscreen", "video",
    "audio", "image", "gallery", "upload", "download", "stream"
  };
}

std::vector<std::string> ContextualRelevanceScorer::ExtractSemanticKeywords(const std::string& text) {
  std::vector<std::string> keywords;
  std::string normalized;

  // Normalize: lowercase and split on non-alphanumeric
  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      normalized += std::tolower(static_cast<unsigned char>(c));
    } else if (!normalized.empty() && normalized.back() != ' ') {
      normalized += ' ';
    }
  }

  // Extract words
  std::istringstream iss(normalized);
  std::string word;
  std::unordered_set<std::string> stopWords = {
    "the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for",
    "is", "it", "of", "this", "that", "with", "as", "be", "are", "was"
  };

  while (iss >> word) {
    if (word.length() >= 2 && stopWords.find(word) == stopWords.end()) {
      keywords.push_back(word);
    }
  }

  return keywords;
}

std::vector<std::string> ContextualRelevanceScorer::ExpandQuery(const std::string& query) {
  std::vector<std::string> expanded;
  auto keywords = ExtractSemanticKeywords(query);

  // Add original keywords
  for (const auto& kw : keywords) {
    expanded.push_back(kw);

    // Add synonyms
    auto it = synonyms_.find(kw);
    if (it != synonyms_.end()) {
      expanded.insert(expanded.end(), it->second.begin(), it->second.end());
    }
  }

  return expanded;
}

std::string ContextualRelevanceScorer::InferActionType(const std::string& query) {
  std::string normalized;
  for (char c : query) {
    normalized += std::tolower(static_cast<unsigned char>(c));
  }

  // Check for explicit action verbs
  if (normalized.find("click") != std::string::npos ||
      normalized.find("tap") != std::string::npos ||
      normalized.find("press") != std::string::npos) {
    return "click";
  }

  if (normalized.find("type") != std::string::npos ||
      normalized.find("enter") != std::string::npos ||
      normalized.find("fill") != std::string::npos ||
      normalized.find("input") != std::string::npos) {
    return "type";
  }

  if (normalized.find("select") != std::string::npos ||
      normalized.find("choose") != std::string::npos ||
      normalized.find("pick") != std::string::npos ||
      normalized.find("dropdown") != std::string::npos) {
    return "select";
  }

  if (normalized.find("check") != std::string::npos ||
      normalized.find("checkbox") != std::string::npos ||
      normalized.find("toggle") != std::string::npos ||
      normalized.find("tick") != std::string::npos) {
    return "check";
  }

  if (normalized.find("submit") != std::string::npos ||
      normalized.find("send") != std::string::npos) {
    return "submit";
  }

  if (normalized.find("search") != std::string::npos ||
      normalized.find("find") != std::string::npos) {
    return "search";
  }

  // Infer from element type keywords
  if (normalized.find("button") != std::string::npos ||
      normalized.find("link") != std::string::npos) {
    return "click";
  }

  if (normalized.find("field") != std::string::npos ||
      normalized.find("box") != std::string::npos ||
      normalized.find("input") != std::string::npos) {
    return "type";
  }

  return "click";  // Default action
}

std::vector<std::string> ContextualRelevanceScorer::GetExpectedElementTypes(const std::string& action) {
  auto it = actionToElements_.find(action);
  if (it != actionToElements_.end()) {
    return it->second;
  }
  return {"button", "input", "a"};  // Default expected types
}

bool ContextualRelevanceScorer::WordsRelated(const std::string& word1, const std::string& word2) {
  if (word1 == word2) return true;

  // Check synonym database
  auto it = synonyms_.find(word1);
  if (it != synonyms_.end()) {
    for (const auto& syn : it->second) {
      if (syn == word2) return true;
    }
  }

  it = synonyms_.find(word2);
  if (it != synonyms_.end()) {
    for (const auto& syn : it->second) {
      if (syn == word1) return true;
    }
  }

  // Check if one is substring of other (for compound words)
  if (word1.length() >= 3 && word2.length() >= 3) {
    if (word1.find(word2) != std::string::npos ||
        word2.find(word1) != std::string::npos) {
      return true;
    }
  }

  return false;
}

float ContextualRelevanceScorer::CalculateSynonymOverlap(
    const std::vector<std::string>& queryWords,
    const std::vector<std::string>& elemWords) {

  if (queryWords.empty() || elemWords.empty()) {
    return 0.0f;
  }

  int matches = 0;
  for (const auto& qw : queryWords) {
    for (const auto& ew : elemWords) {
      if (WordsRelated(qw, ew)) {
        matches++;
        break;  // Count each query word once
      }
    }
  }

  return static_cast<float>(matches) / queryWords.size();
}

float ContextualRelevanceScorer::ScoreRoleMatch(const std::string& elemRole, const std::string& query) {
  if (elemRole.empty()) {
    return 0.0f;
  }

  std::string action = InferActionType(query);
  auto expectedTypes = GetExpectedElementTypes(action);

  // Check if element role contains expected types
  std::string lowerRole = elemRole;
  std::transform(lowerRole.begin(), lowerRole.end(), lowerRole.begin(), ::tolower);

  for (const auto& expected : expectedTypes) {
    if (lowerRole.find(expected) != std::string::npos) {
      return 1.0f;
    }
  }

  // Check for semantic role matches
  auto queryKeywords = ExtractSemanticKeywords(query);
  for (const auto& kw : queryKeywords) {
    if (lowerRole.find(kw) != std::string::npos) {
      return 0.8f;
    }
  }

  return 0.2f;  // No match
}

float ContextualRelevanceScorer::ScoreLabelRelationship(const ElementSemantics& elem, const std::string& query) {
  // Check if nearby_text (label) matches query
  if (elem.nearby_text.empty()) {
    return 0.0f;
  }

  auto textScorer = TextSimilarityScorer::GetInstance();
  float labelScore = textScorer->Score(query, elem.nearby_text);

  // Boost if label is explicitly associated (label_for attribute)
  if (!elem.label_for.empty() || !elem.id.empty()) {
    labelScore = std::min(1.0f, labelScore + 0.1f);
  }

  return labelScore;
}

float ContextualRelevanceScorer::ScoreNearbyContext(const ElementSemantics& elem, const std::string& query) {
  // Combine all text sources with different weights
  std::vector<std::pair<std::string, float>> textSources = {
    {elem.aria_label, 1.2f},    // Highest weight - accessibility label
    {elem.placeholder, 1.1f},   // High weight - describes input purpose
    {elem.text, 1.0f},          // Normal weight - visible text
    {elem.title, 0.9f},         // Lower weight - tooltip
    {elem.name, 0.7f},          // Developer name, less reliable
    {elem.value, 0.6f}          // Current value
  };

  float bestScore = 0.0f;
  auto textScorer = TextSimilarityScorer::GetInstance();

  for (const auto& [text, weight] : textSources) {
    if (!text.empty()) {
      float score = textScorer->Score(query, text) * weight;
      bestScore = std::max(bestScore, score);
    }
  }

  return std::min(1.0f, bestScore);
}

float ContextualRelevanceScorer::ScoreDomainRelevance(const ElementSemantics& elem, const std::string& query) {
  auto queryKeywords = ExtractSemanticKeywords(query);

  // Combine all element text
  std::string allText = elem.text + " " + elem.aria_label + " " + elem.placeholder +
                        " " + elem.title + " " + elem.name + " " + elem.nearby_text;
  auto elemKeywords = ExtractSemanticKeywords(allText);

  // Determine which domain the query belongs to
  std::string bestDomain;
  int bestDomainMatches = 0;

  for (const auto& [domain, vocab] : domainClusters_) {
    int matches = 0;
    for (const auto& qw : queryKeywords) {
      if (vocab.count(qw)) {
        matches++;
      }
    }
    if (matches > bestDomainMatches) {
      bestDomainMatches = matches;
      bestDomain = domain;
    }
  }

  if (bestDomain.empty()) {
    return 0.5f;  // No domain identified - neutral score
  }

  // Check if element belongs to same domain
  const auto& domainVocab = domainClusters_[bestDomain];
  int elemDomainMatches = 0;
  for (const auto& ew : elemKeywords) {
    if (domainVocab.count(ew)) {
      elemDomainMatches++;
    }
  }

  if (elemDomainMatches > 0) {
    return 0.7f + std::min(0.3f, elemDomainMatches * 0.1f);
  }

  return 0.3f;  // Different domain
}

float ContextualRelevanceScorer::Score(const ElementSemantics& elem, const std::string& query) {
  if (query.empty()) {
    return 0.0f;
  }

  // Calculate component scores
  auto queryKeywords = ExtractSemanticKeywords(query);

  // Combine all element text for synonym matching
  std::string allText = elem.text + " " + elem.aria_label + " " + elem.placeholder +
                        " " + elem.title + " " + elem.nearby_text;
  auto elemKeywords = ExtractSemanticKeywords(allText);

  float synonymScore = CalculateSynonymOverlap(queryKeywords, elemKeywords);
  float actionScore = ScoreRoleMatch(elem.inferred_role, query);
  float labelScore = ScoreLabelRelationship(elem, query);
  float contextScore = ScoreNearbyContext(elem, query);
  float domainScore = ScoreDomainRelevance(elem, query);

  // Weighted combination
  float score = synonymScore * kSynonymMatchWeight +
                actionScore * kActionMatchWeight +
                labelScore * kLabelRelationWeight +
                contextScore * kNearbyContextWeight +
                domainScore * kDomainRelevanceWeight;

  // Bonus for high scores across multiple dimensions
  int highScoreCount = 0;
  if (synonymScore > 0.7f) highScoreCount++;
  if (actionScore > 0.7f) highScoreCount++;
  if (labelScore > 0.7f) highScoreCount++;
  if (contextScore > 0.7f) highScoreCount++;

  if (highScoreCount >= 3) {
    score = std::min(1.0f, score + 0.15f);
  } else if (highScoreCount >= 2) {
    score = std::min(1.0f, score + 0.05f);
  }

  return score;
}
