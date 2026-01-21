#include <node_api.h>
#include <memory>
#include <string>
#include <cstring>
#include "owl_browser_manager.h"
#include "action_result.h"

// Helper to convert napi_value to std::string
std::string GetStringFromValue(napi_env env, napi_value value) {
  size_t str_len;
  napi_get_value_string_utf8(env, value, nullptr, 0, &str_len);

  std::string result(str_len, '\0');
  napi_get_value_string_utf8(env, value, &result[0], str_len + 1, nullptr);

  return result;
}

// Initialize browser
napi_value Init(napi_env env, napi_callback_info info) {
  OwlBrowserManager::GetInstance()->Initialize();

  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}

// Shutdown browser
napi_value Shutdown(napi_env env, napi_callback_info info) {
  OwlBrowserManager::GetInstance()->Shutdown();

  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}

// Create browser context
napi_value CreateContext(napi_env env, napi_callback_info info) {
  std::string context_id = OwlBrowserManager::GetInstance()->CreateContext();

  napi_value result;
  napi_create_string_utf8(env, context_id.c_str(), context_id.length(), &result);
  return result;
}

// Release context
napi_value ReleaseContext(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

  if (argc < 1) {
    napi_throw_error(env, nullptr, "context_id required");
    return nullptr;
  }

  std::string context_id = GetStringFromValue(env, argv[0]);
  OwlBrowserManager::GetInstance()->ReleaseContext(context_id);

  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}

// Navigate
napi_value Navigate(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

  if (argc < 2) {
    napi_throw_error(env, nullptr, "context_id and url required");
    return nullptr;
  }

  std::string context_id = GetStringFromValue(env, argv[0]);
  std::string url = GetStringFromValue(env, argv[1]);

  bool success = OwlBrowserManager::GetInstance()->Navigate(context_id, url);

  napi_value result;
  napi_get_boolean(env, success, &result);
  return result;
}

// Click
napi_value Click(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

  if (argc < 2) {
    napi_throw_error(env, nullptr, "context_id and selector required");
    return nullptr;
  }

  std::string context_id = GetStringFromValue(env, argv[0]);
  std::string selector = GetStringFromValue(env, argv[1]);

  // Parse optional verification level (default to STANDARD)
  VerificationLevel level = VerificationLevel::STANDARD;
  if (argc >= 3) {
    std::string level_str = GetStringFromValue(env, argv[2]);
    if (level_str == "none") level = VerificationLevel::NONE;
    else if (level_str == "basic") level = VerificationLevel::BASIC;
    else if (level_str == "strict") level = VerificationLevel::STRICT;
  }

  ActionResult action_result = OwlBrowserManager::GetInstance()->Click(context_id, selector, level);

  // Return boolean for backward compatibility
  napi_value result;
  napi_get_boolean(env, action_result.status == ActionStatus::OK, &result);
  return result;
}

// Type
napi_value Type(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value argv[4];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

  if (argc < 3) {
    napi_throw_error(env, nullptr, "context_id, selector, and text required");
    return nullptr;
  }

  std::string context_id = GetStringFromValue(env, argv[0]);
  std::string selector = GetStringFromValue(env, argv[1]);
  std::string text = GetStringFromValue(env, argv[2]);

  // Parse optional verification level (default to STANDARD)
  VerificationLevel level = VerificationLevel::STANDARD;
  if (argc >= 4) {
    std::string level_str = GetStringFromValue(env, argv[3]);
    if (level_str == "none") level = VerificationLevel::NONE;
    else if (level_str == "basic") level = VerificationLevel::BASIC;
    else if (level_str == "strict") level = VerificationLevel::STRICT;
  }

  ActionResult action_result = OwlBrowserManager::GetInstance()->Type(context_id, selector, text, level);

  // Return boolean for backward compatibility
  napi_value result;
  napi_get_boolean(env, action_result.status == ActionStatus::OK, &result);
  return result;
}

// Extract text
napi_value ExtractText(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

  if (argc < 2) {
    napi_throw_error(env, nullptr, "context_id and selector required");
    return nullptr;
  }

  std::string context_id = GetStringFromValue(env, argv[0]);
  std::string selector = GetStringFromValue(env, argv[1]);

  std::string text = OwlBrowserManager::GetInstance()->ExtractText(context_id, selector);

  napi_value result;
  napi_create_string_utf8(env, text.c_str(), text.length(), &result);
  return result;
}

// Screenshot
napi_value Screenshot(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

  if (argc < 1) {
    napi_throw_error(env, nullptr, "context_id required");
    return nullptr;
  }

  std::string context_id = GetStringFromValue(env, argv[0]);

  std::vector<uint8_t> png_data = OwlBrowserManager::GetInstance()->Screenshot(context_id);

  napi_value result;
  void* buffer_data;
  napi_create_buffer_copy(env, png_data.size(), png_data.data(), &buffer_data, &result);

  return result;
}

// Module initialization
napi_value Init(napi_env env, napi_value exports) {
  napi_value init_fn, shutdown_fn, create_context_fn, release_context_fn;
  napi_value navigate_fn, click_fn, type_fn, extract_text_fn, screenshot_fn;

  napi_create_function(env, "init", NAPI_AUTO_LENGTH, Init, nullptr, &init_fn);
  napi_create_function(env, "shutdown", NAPI_AUTO_LENGTH, Shutdown, nullptr, &shutdown_fn);
  napi_create_function(env, "createContext", NAPI_AUTO_LENGTH, CreateContext, nullptr, &create_context_fn);
  napi_create_function(env, "releaseContext", NAPI_AUTO_LENGTH, ReleaseContext, nullptr, &release_context_fn);
  napi_create_function(env, "navigate", NAPI_AUTO_LENGTH, Navigate, nullptr, &navigate_fn);
  napi_create_function(env, "click", NAPI_AUTO_LENGTH, Click, nullptr, &click_fn);
  napi_create_function(env, "type", NAPI_AUTO_LENGTH, Type, nullptr, &type_fn);
  napi_create_function(env, "extractText", NAPI_AUTO_LENGTH, ExtractText, nullptr, &extract_text_fn);
  napi_create_function(env, "screenshot", NAPI_AUTO_LENGTH, Screenshot, nullptr, &screenshot_fn);

  napi_set_named_property(env, exports, "init", init_fn);
  napi_set_named_property(env, exports, "shutdown", shutdown_fn);
  napi_set_named_property(env, exports, "createContext", create_context_fn);
  napi_set_named_property(env, exports, "releaseContext", release_context_fn);
  napi_set_named_property(env, exports, "navigate", navigate_fn);
  napi_set_named_property(env, exports, "click", click_fn);
  napi_set_named_property(env, exports, "type", type_fn);
  napi_set_named_property(env, exports, "extractText", extract_text_fn);
  napi_set_named_property(env, exports, "screenshot", screenshot_fn);

  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)