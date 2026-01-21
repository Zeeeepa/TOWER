#include "owl_extraction_template.h"
#include "logger.h"
#include <regex>
#include <sstream>

OlibTemplateManager* OlibTemplateManager::instance_ = nullptr;

OlibTemplateManager::OlibTemplateManager() {
  LoadBuiltinTemplates();
}

OlibTemplateManager::~OlibTemplateManager() {}

OlibTemplateManager* OlibTemplateManager::GetInstance() {
  if (!instance_) {
    instance_ = new OlibTemplateManager();
  }
  return instance_;
}

void OlibTemplateManager::LoadBuiltinTemplates() {
  LOG_DEBUG("TemplateManager", "Loading built-in extraction templates");

  RegisterGoogleSearchTemplate();
  RegisterWikipediaTemplate();
  RegisterAmazonProductTemplate();
  RegisterGitHubRepoTemplate();
  RegisterTwitterFeedTemplate();
  RegisterRedditThreadTemplate();

  LOG_DEBUG("TemplateManager", "Loaded " + std::to_string(templates_.size()) +
           " built-in templates");
}

void OlibTemplateManager::RegisterGoogleSearchTemplate() {
  auto templ = std::make_unique<ExtractionTemplate>();

  templ->name = "google_search";
  templ->version = "1.0.0";
  templ->description = "Extract Google search results with pagination";

  // Detection rules
  templ->detection.url_patterns = {
    R"(^https?://www\.google\.[^/]+/search)",
    R"(^https?://google\.[^/]+/search)"
  };
  templ->detection.confidence_threshold = 0.9;

  // Extraction rules
  FieldRule query_rule;
  query_rule.selector = "input[name='q']";
  query_rule.extract = ExtractType::ATTRIBUTE;
  query_rule.attribute = "value";
  query_rule.transform = Transform::TRIM;
  templ->extraction["query"] = query_rule;

  // Search results (multiple)
  FieldRule results_rule;
  results_rule.selector = "div.g, div[data-sokoban-container]";
  results_rule.multiple = true;

  // Title field
  FieldRule title_field;
  title_field.selector = "h3";
  title_field.extract = ExtractType::TEXT;
  title_field.transform = Transform::TRIM;
  results_rule.fields["title"] = title_field;

  // URL field
  FieldRule url_field;
  url_field.selector = "a";
  url_field.extract = ExtractType::ATTRIBUTE;
  url_field.attribute = "href";
  url_field.transform = Transform::ABSOLUTE_URL;
  results_rule.fields["url"] = url_field;

  // Snippet field
  FieldRule snippet_field;
  snippet_field.selector = "div.VwiC3b, span.aCOpRe";
  snippet_field.extract = ExtractType::TEXT;
  snippet_field.transform = Transform::CLEAN_WHITESPACE;
  results_rule.fields["snippet"] = snippet_field;

  templ->extraction["results"] = results_rule;

  // Pagination
  FieldRule next_page_rule;
  next_page_rule.selector = "a#pnnext";
  next_page_rule.extract = ExtractType::ATTRIBUTE;
  next_page_rule.attribute = "href";
  next_page_rule.transform = Transform::ABSOLUTE_URL;
  templ->extraction["next_page"] = next_page_rule;

  templates_["google_search"] = std::move(templ);
  LOG_DEBUG("TemplateManager", "Registered template: google_search");
}

void OlibTemplateManager::RegisterWikipediaTemplate() {
  auto templ = std::make_unique<ExtractionTemplate>();

  templ->name = "wikipedia";
  templ->version = "1.0.0";
  templ->description = "Extract Wikipedia article content with sections";

  // Detection
  templ->detection.url_patterns = {
    R"(^https?://[a-z]+\.wikipedia\.org/wiki/)"
  };

  // Title
  FieldRule title_rule;
  title_rule.selector = "h1#firstHeading, h1.firstHeading";
  title_rule.extract = ExtractType::TEXT;
  title_rule.transform = Transform::TRIM;
  templ->extraction["title"] = title_rule;

  // Main content
  FieldRule content_rule;
  content_rule.selector = "div#mw-content-text";
  content_rule.extract = ExtractType::TEXT;
  content_rule.transform = Transform::CLEAN_WHITESPACE;
  templ->extraction["content"] = content_rule;

  // Sections (multiple)
  FieldRule sections_rule;
  sections_rule.selector = "h2, h3";
  sections_rule.multiple = true;
  sections_rule.extract = ExtractType::TEXT;
  sections_rule.transform = Transform::TRIM;
  templ->extraction["sections"] = sections_rule;

  // Infobox (if exists)
  FieldRule infobox_rule;
  infobox_rule.selector = "table.infobox";
  infobox_rule.extract = ExtractType::TEXT;
  infobox_rule.transform = Transform::CLEAN_WHITESPACE;
  templ->extraction["infobox"] = infobox_rule;

  templates_["wikipedia"] = std::move(templ);
  LOG_DEBUG("TemplateManager", "Registered template: wikipedia");
}

void OlibTemplateManager::RegisterAmazonProductTemplate() {
  auto templ = std::make_unique<ExtractionTemplate>();

  templ->name = "amazon_product";
  templ->version = "1.0.0";
  templ->description = "Extract Amazon product information";

  // Detection
  templ->detection.url_patterns = {
    R"(^https?://www\.amazon\.[^/]+/.*/(dp|gp/product)/)"
  };

  // Product title
  FieldRule title_rule;
  title_rule.selector = "span#productTitle";
  title_rule.extract = ExtractType::TEXT;
  title_rule.transform = Transform::TRIM;
  templ->extraction["title"] = title_rule;

  // Price
  FieldRule price_rule;
  price_rule.selector = "span.a-price span.a-offscreen, span.a-price-whole";
  price_rule.extract = ExtractType::TEXT;
  price_rule.transform = Transform::PARSE_PRICE;
  templ->extraction["price"] = price_rule;

  // Rating
  FieldRule rating_rule;
  rating_rule.selector = "span.a-icon-alt";
  rating_rule.extract = ExtractType::TEXT;
  rating_rule.transform = Transform::PARSE_FLOAT;
  templ->extraction["rating"] = rating_rule;

  // Review count
  FieldRule reviews_rule;
  reviews_rule.selector = "span#acrCustomerReviewText";
  reviews_rule.extract = ExtractType::TEXT;
  reviews_rule.transform = Transform::PARSE_INT;
  templ->extraction["review_count"] = reviews_rule;

  // Description
  FieldRule desc_rule;
  desc_rule.selector = "div#feature-bullets, div#productDescription";
  desc_rule.extract = ExtractType::TEXT;
  desc_rule.transform = Transform::CLEAN_WHITESPACE;
  templ->extraction["description"] = desc_rule;

  // Availability
  FieldRule avail_rule;
  avail_rule.selector = "div#availability span";
  avail_rule.extract = ExtractType::TEXT;
  avail_rule.transform = Transform::TRIM;
  templ->extraction["availability"] = avail_rule;

  templates_["amazon_product"] = std::move(templ);
  LOG_DEBUG("TemplateManager", "Registered template: amazon_product");
}

void OlibTemplateManager::RegisterGitHubRepoTemplate() {
  auto templ = std::make_unique<ExtractionTemplate>();

  templ->name = "github_repo";
  templ->version = "1.0.0";
  templ->description = "Extract GitHub repository information";

  // Detection
  templ->detection.url_patterns = {
    R"(^https?://github\.com/[^/]+/[^/]+/?$)"
  };

  // Repo name
  FieldRule name_rule;
  name_rule.selector = "strong[itemprop='name'] a";
  name_rule.extract = ExtractType::TEXT;
  name_rule.transform = Transform::TRIM;
  templ->extraction["name"] = name_rule;

  // Description
  FieldRule desc_rule;
  desc_rule.selector = "p[itemprop='about'], p.f4";
  desc_rule.extract = ExtractType::TEXT;
  desc_rule.transform = Transform::TRIM;
  templ->extraction["description"] = desc_rule;

  // Stars
  FieldRule stars_rule;
  stars_rule.selector = "span#repo-stars-counter-star";
  stars_rule.extract = ExtractType::TEXT;
  stars_rule.transform = Transform::PARSE_INT;
  templ->extraction["stars"] = stars_rule;

  // Forks
  FieldRule forks_rule;
  forks_rule.selector = "span#repo-network-counter";
  forks_rule.extract = ExtractType::TEXT;
  forks_rule.transform = Transform::PARSE_INT;
  templ->extraction["forks"] = forks_rule;

  templates_["github_repo"] = std::move(templ);
  LOG_DEBUG("TemplateManager", "Registered template: github_repo");
}

void OlibTemplateManager::RegisterTwitterFeedTemplate() {
  auto templ = std::make_unique<ExtractionTemplate>();

  templ->name = "twitter_feed";
  templ->version = "1.0.0";
  templ->description = "Extract Twitter/X feed tweets";

  // Detection
  templ->detection.url_patterns = {
    R"(^https?://(twitter\.com|x\.com)/)"
  };

  // Tweets (multiple)
  FieldRule tweets_rule;
  tweets_rule.selector = "article[data-testid='tweet']";
  tweets_rule.multiple = true;

  // Author field
  FieldRule author_field;
  author_field.selector = "div[data-testid='User-Name']";
  author_field.extract = ExtractType::TEXT;
  author_field.transform = Transform::TRIM;
  tweets_rule.fields["author"] = author_field;

  // Tweet text field
  FieldRule text_field;
  text_field.selector = "div[data-testid='tweetText']";
  text_field.extract = ExtractType::TEXT;
  text_field.transform = Transform::TRIM;
  tweets_rule.fields["text"] = text_field;

  templ->extraction["tweets"] = tweets_rule;

  templates_["twitter_feed"] = std::move(templ);
  LOG_DEBUG("TemplateManager", "Registered template: twitter_feed");
}

void OlibTemplateManager::RegisterRedditThreadTemplate() {
  auto templ = std::make_unique<ExtractionTemplate>();

  templ->name = "reddit_thread";
  templ->version = "1.0.0";
  templ->description = "Extract Reddit thread and comments";

  // Detection
  templ->detection.url_patterns = {
    R"(^https?://www\.reddit\.com/r/[^/]+/comments/)"
  };

  // Post title
  FieldRule title_rule;
  title_rule.selector = "h1";
  title_rule.extract = ExtractType::TEXT;
  title_rule.transform = Transform::TRIM;
  templ->extraction["title"] = title_rule;

  // Post content
  FieldRule content_rule;
  content_rule.selector = "div[data-test-id='post-content']";
  content_rule.extract = ExtractType::TEXT;
  content_rule.transform = Transform::CLEAN_WHITESPACE;
  templ->extraction["content"] = content_rule;

  templates_["reddit_thread"] = std::move(templ);
  LOG_DEBUG("TemplateManager", "Registered template: reddit_thread");
}

void OlibTemplateManager::LoadTemplate(const std::string& json) {
  // TODO: Parse JSON and create template
  LOG_DEBUG("TemplateManager", "Loading template from JSON");
}

void OlibTemplateManager::LoadTemplateFromFile(const std::string& path) {
  // TODO: Read file and load template
  LOG_DEBUG("TemplateManager", "Loading template from file: " + path);
}

ExtractionTemplate* OlibTemplateManager::GetTemplate(const std::string& name) {
  auto it = templates_.find(name);
  if (it != templates_.end()) {
    return it->second.get();
  }
  return nullptr;
}

ExtractionTemplate* OlibTemplateManager::DetectTemplate(
    const std::string& url,
    const std::string& html,
    const std::map<std::string, std::string>& meta_tags) {

  LOG_DEBUG("TemplateManager", "Detecting template for URL: " + url);

  // Try URL pattern matching first (fastest)
  for (auto& [name, templ] : templates_) {
    for (const auto& pattern : templ->detection.url_patterns) {
      std::regex regex(pattern);
      if (std::regex_search(url, regex)) {
        LOG_DEBUG("TemplateManager", "Matched template: " + name);
        return templ.get();
      }
    }
  }

  LOG_DEBUG("TemplateManager", "No template matched, using generic");
  return nullptr;
}

std::vector<std::string> OlibTemplateManager::ListTemplates() const {
  std::vector<std::string> names;
  for (const auto& [name, templ] : templates_) {
    names.push_back(name);
  }
  return names;
}

// Template JSON serialization (placeholder)
std::unique_ptr<ExtractionTemplate> ExtractionTemplate::FromJSON(const std::string& json) {
  // TODO: Implement JSON parsing
  return nullptr;
}

std::string ExtractionTemplate::ToJSON() const {
  // TODO: Implement JSON serialization
  return "{}";
}
