#include "owl_image_captcha_provider_base.h"
#include "owl_llm_client.h"
#include "logger.h"
#include <sstream>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <set>

// Helper: Draw a filled rectangle on BGRA buffer with alpha blending
static void DrawFilledRect(uint8_t* bgra, int img_w, int img_h, int x, int y, int w, int h,
                           uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
  float alpha = a / 255.0f;
  float inv_alpha = 1.0f - alpha;

  for (int py = y; py < y + h && py < img_h; py++) {
    for (int px = x; px < x + w && px < img_w; px++) {
      if (px >= 0 && py >= 0) {
        int idx = (py * img_w + px) * 4;
        // Alpha blend with existing pixel
        bgra[idx + 0] = static_cast<uint8_t>(b * alpha + bgra[idx + 0] * inv_alpha);
        bgra[idx + 1] = static_cast<uint8_t>(g * alpha + bgra[idx + 1] * inv_alpha);
        bgra[idx + 2] = static_cast<uint8_t>(r * alpha + bgra[idx + 2] * inv_alpha);
        bgra[idx + 3] = 255;  // Full opacity for final pixel
      }
    }
  }
}

// Helper: Draw a clean digit on BGRA buffer (smooth, sans-serif style)
static void DrawDigit(uint8_t* bgra, int img_w, int img_h, int x, int y, int digit,
                      uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
  // Clean 7x9 bitmap font for digits 0-9 (sans-serif inspired)
  static const uint8_t font[10][9] = {
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C}, // 0
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E}, // 1
    {0x3C, 0x66, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E}, // 2
    {0x3C, 0x66, 0x06, 0x06, 0x1C, 0x06, 0x06, 0x66, 0x3C}, // 3
    {0x0C, 0x1C, 0x2C, 0x4C, 0x4C, 0x7E, 0x0C, 0x0C, 0x0C}, // 4
    {0x7E, 0x60, 0x60, 0x7C, 0x06, 0x06, 0x06, 0x66, 0x3C}, // 5
    {0x1C, 0x30, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C}, // 6
    {0x7E, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x30}, // 7
    {0x3C, 0x66, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x3C}, // 8
    {0x3C, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0C, 0x38}  // 9
  };

  if (digit < 0 || digit > 9) return;

  for (int row = 0; row < 9; row++) {
    for (int col = 0; col < 7; col++) {
      if (font[digit][row] & (1 << (6 - col))) {
        int px = x + col;
        int py = y + row;
        if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
          int idx = (py * img_w + px) * 4;
          bgra[idx + 0] = b;
          bgra[idx + 1] = g;
          bgra[idx + 2] = r;
          bgra[idx + 3] = a;
        }
      }
    }
  }
}

ImageCaptchaProviderBase::ImageCaptchaProviderBase()
    : auto_submit_(true), allow_skip_(true) {
}

void ImageCaptchaProviderBase::Wait(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string ImageCaptchaProviderBase::ExecuteJavaScript(
    CefRefPtr<CefBrowser> browser,
    const std::string& script) {
  // For synchronous JS execution, we rely on other mechanisms
  // This is a placeholder - actual implementation uses CEF's async JS execution
  if (browser && browser->GetMainFrame()) {
    browser->GetMainFrame()->ExecuteJavaScript(script, browser->GetMainFrame()->GetURL(), 0);
  }
  return "";
}

bool ImageCaptchaProviderBase::IsElementVisible(
    CefRefPtr<CefBrowser> browser,
    const std::string& selector) {
  // This would typically check via render tracker or JS execution
  // Subclasses should implement provider-specific visibility checks
  return true;
}

bool ImageCaptchaProviderBase::ClickElementByPosition(
    CefRefPtr<CefBrowser> browser,
    int x, int y, int width, int height) {
  if (!browser || !browser->GetHost()) {
    return false;
  }

  auto host = browser->GetHost();
  host->SetFocus(true);

  CefMouseEvent mouse_event;
  mouse_event.x = x + (width / 2);
  mouse_event.y = y + (height / 2);

  host->SendMouseMoveEvent(mouse_event, false);
  Wait(50);  // Small delay for natural movement
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);  // Mouse down
  host->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);   // Mouse up

  return true;
}

bool ImageCaptchaProviderBase::ScrollIntoView(
    CefRefPtr<CefBrowser> browser,
    const std::string& selector) {
  if (!browser || !browser->GetMainFrame()) {
    return false;
  }

  std::string script = "(function() {"
                       "  var elem = document.querySelector('" + selector + "');"
                       "  if (elem) {"
                       "    elem.scrollIntoView({behavior: 'instant', block: 'center'});"
                       "    return true;"
                       "  }"
                       "  return false;"
                       "})();";

  browser->GetMainFrame()->ExecuteJavaScript(script, browser->GetMainFrame()->GetURL(), 0);
  Wait(300);  // Wait for scroll
  return true;
}

int ImageCaptchaProviderBase::GetRandomClickDelay() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(config_.click_delay_min_ms, config_.click_delay_max_ms);
  return dist(gen);
}

std::string ImageCaptchaProviderBase::Base64Encode(const std::vector<uint8_t>& data) {
  static const char base64_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

  std::string encoded;
  encoded.reserve(((data.size() + 2) / 3) * 4);

  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t triple = (data[i] << 16);
    if (i + 1 < data.size()) triple |= (data[i + 1] << 8);
    if (i + 2 < data.size()) triple |= data[i + 2];

    encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
    encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
    encoded.push_back((i + 1 < data.size()) ? base64_chars[(triple >> 6) & 0x3F] : '=');
    encoded.push_back((i + 2 < data.size()) ? base64_chars[triple & 0x3F] : '=');
  }

  return encoded;
}

// Helper function to get object-specific recognition hints
static std::string GetObjectHints(const std::string& target) {
  // Normalize target to lowercase for matching
  std::string lower_target = target;
  std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);

  // Object-specific hints to help vision models identify targets accurately
  if (lower_target.find("car") != std::string::npos) {
    return "Look for: sedans, SUVs, trucks, vans, hatchbacks. "
           "Cars have 4 wheels, windows, headlights, and a distinct body shape. "
           "Include parked cars, moving cars, partial cars. Exclude motorcycles and bicycles.";
  }
  if (lower_target.find("bus") != std::string::npos) {
    return "Look for: large rectangular vehicles, school buses (yellow), city buses, tour buses. "
           "Buses are taller and longer than cars with multiple windows in a row. "
           "Include partial buses where you can see the distinctive shape.";
  }
  if (lower_target.find("bicycle") != std::string::npos || lower_target.find("bike") != std::string::npos) {
    return "Look for: two-wheeled vehicles with pedals, handlebars, and a seat. "
           "Include mountain bikes, road bikes, city bikes, bike shares. "
           "May be ridden by a person or parked. Exclude motorcycles (have engines).";
  }
  if (lower_target.find("motorcycle") != std::string::npos) {
    return "Look for: two-wheeled motorized vehicles with engines. "
           "Motorcycles have engines, exhaust pipes, and are heavier than bicycles. "
           "Include sport bikes, cruisers, scooters. Exclude bicycles (no engine).";
  }
  if (lower_target.find("traffic light") != std::string::npos) {
    return "Look for: vertical poles with red/yellow/green light signals. "
           "Traffic lights hang over roads or stand on poles at intersections. "
           "Include any visible traffic light regardless of which color is lit.";
  }
  if (lower_target.find("fire hydrant") != std::string::npos || lower_target.find("hydrant") != std::string::npos) {
    return "Look for: short metal posts on sidewalks, usually red or yellow. "
           "Fire hydrants have a distinctive rounded top with outlet caps on sides. "
           "They are about waist-high and stand alone near curbs.";
  }
  if (lower_target.find("crosswalk") != std::string::npos) {
    return "Look for: white painted stripes on road surfaces for pedestrians to cross. "
           "Crosswalks are rectangular patterns of parallel white lines on pavement. "
           "May be at intersections or mid-block. Include zebra crossings.";
  }
  if (lower_target.find("stair") != std::string::npos) {
    return "Look for: series of horizontal steps going up or down. "
           "Stairs have multiple levels/treads. Include indoor stairs, outdoor stairs, "
           "escalators, stoops, building entrances with steps.";
  }
  if (lower_target.find("bridge") != std::string::npos) {
    return "Look for: structures that span over water, roads, or valleys. "
           "Bridges have railings, support structures. Include footbridges, "
           "highway overpasses, suspension bridges, pedestrian bridges.";
  }
  if (lower_target.find("palm") != std::string::npos || lower_target.find("tree") != std::string::npos) {
    return "Look for: trees with distinctive features. Palm trees have tall trunks with "
           "fan-shaped or feather-shaped leaves at the top. Regular trees have branches "
           "and leaves/foliage. Include partial trees visible in frame.";
  }
  if (lower_target.find("parking meter") != std::string::npos || lower_target.find("meter") != std::string::npos) {
    return "Look for: tall metal poles with a display/coin slot near the top. "
           "Parking meters stand on sidewalks next to parking spots. "
           "May be digital or old-style coin-operated.";
  }
  if (lower_target.find("chimney") != std::string::npos) {
    return "Look for: vertical structures on rooftops for smoke ventilation. "
           "Chimneys are usually brick or metal, rectangular or cylindrical. "
           "They project upward from the roof of buildings.";
  }
  if (lower_target.find("boat") != std::string::npos) {
    return "Look for: watercraft floating on water. Include sailboats, motorboats, "
           "kayaks, canoes, yachts, fishing boats. Boats have hulls and may have "
           "masts, sails, or motors visible.";
  }
  if (lower_target.find("airplane") != std::string::npos || lower_target.find("plane") != std::string::npos ||
      lower_target.find("aircraft") != std::string::npos) {
    return "Look for: aircraft in the sky or on the ground. Include commercial jets, "
           "small propeller planes, private aircraft. Airplanes have wings, fuselage, "
           "and tail sections. May be flying, parked, or at airports.";
  }
  if (lower_target.find("taxi") != std::string::npos) {
    return "Look for: cars marked as taxis, often yellow in US, black in UK. "
           "Taxis have roof signs, markings, or distinctive colors. "
           "Include any vehicle clearly identifiable as a taxi cab.";
  }
  if (lower_target.find("tractor") != std::string::npos) {
    return "Look for: large agricultural vehicles with big rear wheels. "
           "Tractors have an exposed engine, cab, and often attachments. "
           "Used for farming - distinct from construction equipment.";
  }
  if (lower_target.find("storefront") != std::string::npos || lower_target.find("store") != std::string::npos) {
    return "Look for: the front facade of retail shops with display windows. "
           "Storefronts have signs, glass windows showing products, and entrance doors. "
           "Include any visible shop front or retail business entrance.";
  }

  // Default hint for unknown targets
  return "Look carefully for \"" + target + "\" in each image. "
         "Select all images that clearly contain this object.";
}

std::string ImageCaptchaProviderBase::BuildVisionPrompt(
    const std::string& target_description,
    int grid_size,
    const std::string& custom_template) {

  // Prompt optimized for vision models with object-specific hints
  std::stringstream prompt_stream;

  // Get object-specific hints
  std::string hints = GetObjectHints(target_description);

  if (grid_size == 16) {
    // 4x4 grid = ONE image split into 16 squares
    prompt_stream << "TASK: Find \"" << target_description << "\" in a 4x4 grid image.\n\n";
    prompt_stream << "GRID INFO: This is ONE photo divided into 16 squares (4 rows x 4 columns).\n";
    prompt_stream << "Each square has a small red number (0-15) in the top-left corner.\n";
    prompt_stream << "The object may span MULTIPLE adjacent squares.\n\n";
    prompt_stream << "WHAT TO LOOK FOR:\n" << hints << "\n\n";
    prompt_stream << "IMPORTANT: Select ALL squares that contain ANY part of the target object.\n";
    prompt_stream << "If the object spans squares 5,6,9,10 - include all four.\n\n";
    prompt_stream << "OUTPUT: Only comma-separated numbers. Example: 5,6,9,10";
  } else {
    // 3x3 grid = 9 SEPARATE images
    prompt_stream << "TASK: Find \"" << target_description << "\" in 9 separate photos.\n\n";
    prompt_stream << "GRID INFO: This shows 9 DIFFERENT photos arranged in a 3x3 grid.\n";
    prompt_stream << "Each photo has a small red number (0-8) in the top-left corner.\n";
    prompt_stream << "Judge each photo independently - they are NOT connected.\n\n";
    prompt_stream << "WHAT TO LOOK FOR:\n" << hints << "\n\n";
    prompt_stream << "IMPORTANT: Select photos where the target is clearly visible.\n";
    prompt_stream << "When unsure, include the image - it's better to over-select.\n\n";
    prompt_stream << "OUTPUT: Only comma-separated numbers. Example: 2,5,7";
  }

  return prompt_stream.str();
}

std::vector<int> ImageCaptchaProviderBase::ParseVisionResponse(
    const std::string& response,
    int grid_size) {

  std::vector<int> indices;
  std::set<int> seen_indices;  // Prevent duplicates

  // Trim whitespace
  std::string trimmed = response;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
  trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

  // Check for "none"
  std::string lower_response = trimmed;
  std::transform(lower_response.begin(), lower_response.end(), lower_response.begin(), ::tolower);

  if (lower_response.find("none") != std::string::npos) {
    return indices;  // Empty - no matches
  }

  // Parse comma-separated indices
  std::stringstream ss(trimmed);
  std::string item;

  while (std::getline(ss, item, ',')) {
    // Trim whitespace from each item
    item.erase(0, item.find_first_not_of(" \t\n\r"));
    item.erase(item.find_last_not_of(" \t\n\r") + 1);

    // Convert to integer (simple parsing without exceptions)
    if (!item.empty() && item.find_first_not_of("0123456789") == std::string::npos) {
      int index = std::atoi(item.c_str());

      // Validate index range and prevent duplicates
      if (index >= 0 && index < grid_size && seen_indices.find(index) == seen_indices.end()) {
        indices.push_back(index);
        seen_indices.insert(index);
      }
    }
  }

  return indices;
}

std::vector<int> ImageCaptchaProviderBase::IdentifyMatchingImages(
    const std::vector<uint8_t>& grid_screenshot,
    const std::string& target_description,
    int grid_size,
    OwlLLMClient* llm_client) {

  LOG_DEBUG("ImageCaptchaProviderBase", "Identifying images matching: '" +
            target_description + "' (grid size: " + std::to_string(grid_size) + ")");

  if (!llm_client) {
    LOG_ERROR("ImageCaptchaProviderBase", "LLM client not available");
    return {};
  }

  // Convert image to base64
  std::string image_base64 = Base64Encode(grid_screenshot);

  // Build vision prompt
  std::string prompt = BuildVisionPrompt(target_description, grid_size, config_.vision_prompt_template);

  // System prompt: Focused on accurate object detection
  std::string system_prompt = "You are an expert image analyst. Analyze the grid image carefully. "
                              "Output ONLY the numbers of squares/photos containing the target object. "
                              "Format: comma-separated numbers (e.g., 0,3,5). No explanations.";

  // Call vision model
  LOG_DEBUG("ImageCaptchaProviderBase", "Calling vision model for grid analysis...");
  LOG_DEBUG("ImageCaptchaProviderBase", "Prompt: " + prompt.substr(0, 300) + "...");

  auto response = llm_client->CompleteWithImage(
      prompt,
      image_base64,
      system_prompt,
      50,    // max_tokens - allow for longer responses if needed
      0.2    // slightly higher temperature for better reasoning
  );

  if (!response.success) {
    LOG_ERROR("ImageCaptchaProviderBase", "Vision model error: " + response.error);
    return {};
  }

  LOG_DEBUG("ImageCaptchaProviderBase", "Vision model raw response: '" + response.content + "'");

  // Parse response
  std::vector<int> indices = ParseVisionResponse(response.content, grid_size);

  LOG_DEBUG("ImageCaptchaProviderBase", "Parsed " + std::to_string(indices.size()) +
           " matching indices");

  return indices;
}

void ImageCaptchaProviderBase::DrawNumberedOverlays(
    uint8_t* bgra, int img_w, int img_h,
    const std::vector<std::tuple<int, int, int, int>>& grid_items,
    int crop_x, int crop_y) {

  for (size_t i = 0; i < grid_items.size() && i < 16; i++) {
    auto& [gx, gy, gw, gh] = grid_items[i];

    // Calculate position relative to cropped image - smaller padding
    int local_x = gx - crop_x + 2;  // 2px padding from left
    int local_y = gy - crop_y + 2;  // 2px padding from top

    // SMALLER badge: 12x11 pixels with semi-transparent background
    // Semi-transparent dark red background (alpha ~160 for 60% opacity)
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y, 12, 11, 20, 20, 180, 160);

    // Thin white border (1px) for visibility
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y, 12, 1, 255, 255, 255, 200);  // Top
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y + 10, 12, 1, 255, 255, 255, 200);  // Bottom
    DrawFilledRect(bgra, img_w, img_h, local_x, local_y, 1, 11, 255, 255, 255, 200);  // Left
    DrawFilledRect(bgra, img_w, img_h, local_x + 11, local_y, 1, 11, 255, 255, 255, 200);  // Right

    // Draw digit with dark outline for visibility, then white digit on top
    // Dark outline (draw digit shifted in all directions)
    for (int dx = -1; dx <= 1; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        if (dx != 0 || dy != 0) {
          DrawDigit(bgra, img_w, img_h, local_x + 3 + dx, local_y + 1 + dy, static_cast<int>(i), 0, 0, 0, 255);
        }
      }
    }
    // White digit on top
    DrawDigit(bgra, img_w, img_h, local_x + 3, local_y + 1, static_cast<int>(i), 255, 255, 255, 255);
  }
}
