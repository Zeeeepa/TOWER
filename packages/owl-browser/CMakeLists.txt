cmake_minimum_required(VERSION 3.21)
project(owl-browser)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# DEBUG BUILD CONFIGURATION
# ============================================================================
# Enable verbose debug logging only in Debug builds
# Usage: cmake -DCMAKE_BUILD_TYPE=Debug .. for verbose logging
# Usage: cmake -DCMAKE_BUILD_TYPE=Release .. for production (INFO/WARN/ERROR only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(OWL_DEBUG_BUILD=1)
  message(STATUS "Debug build: Verbose logging ENABLED")
else()
  message(STATUS "Release build: Verbose logging DISABLED (INFO/WARN/ERROR only)")
endif()

# Read .env file for configuration
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.env")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.env" ENV_LINES)
  foreach(LINE ${ENV_LINES})
    # Skip comments and empty lines
    if(LINE MATCHES "^#" OR LINE STREQUAL "")
      continue()
    endif()
    # Parse KEY=VALUE
    if(LINE MATCHES "^([^=]+)=(.*)$")
      set(ENV_KEY ${CMAKE_MATCH_1})
      set(ENV_VALUE ${CMAKE_MATCH_2})
      # Remove leading/trailing whitespace
      string(STRIP "${ENV_KEY}" ENV_KEY)
      string(STRIP "${ENV_VALUE}" ENV_VALUE)
      # Remove surrounding quotes if present
      if(ENV_VALUE MATCHES "^\"(.*)\"$")
        set(ENV_VALUE "${CMAKE_MATCH_1}")
      endif()
      # Set as CMake variable
      set(${ENV_KEY} "${ENV_VALUE}")
      message(STATUS "ENV: ${ENV_KEY}=${ENV_VALUE}")
    endif()
  endforeach()
endif()

# License server URL (from .env or default)
if(NOT DEFINED OWL_LICENSE_SERVER_URL)
  set(OWL_LICENSE_SERVER_URL "http://localhost:3034")
endif()
add_compile_definitions(OWL_LICENSE_SERVER_URL="${OWL_LICENSE_SERVER_URL}")

# HMAC secret for nonce authentication (from .env or default)
# This shared secret authenticates requests between browser and license server
if(NOT DEFINED OWL_NONCE_HMAC_SECRET)
  message(WARNING "OWL_NONCE_HMAC_SECRET not set in .env - using insecure default!")
  set(OWL_NONCE_HMAC_SECRET "change-this-in-production-insecure-default-key!")
endif()
add_compile_definitions(OWL_NONCE_HMAC_SECRET="${OWL_NONCE_HMAC_SECRET}")

# Platform detection
if(APPLE)
  set(OS_MACOS 1)
  set(OS_POSIX 1)
  add_compile_definitions(OS_MACOS=1)
elseif(UNIX)
  set(OS_LINUX 1)
  set(OS_POSIX 1)
  add_compile_definitions(OS_LINUX=1)
elseif(WIN32)
  set(OS_WINDOWS 1)
  add_compile_definitions(OS_WINDOWS=1)
endif()

# ============================================================================
# BUILD OPTIONS
# ============================================================================

# BUILD_WITH_LLAMA - Include llama.cpp and AI models in the build
# Default: OFF (lightweight build, use external APIs)
# Set to ON to bundle llama-server and models (~2GB)
# Usage: cmake -DBUILD_WITH_LLAMA=ON ..
option(BUILD_WITH_LLAMA "Bundle llama.cpp server and AI models with the browser" ON)

if(BUILD_WITH_LLAMA)
  message(STATUS "BUILD_WITH_LLAMA=ON - llama-server and models will be bundled (~2GB)")
else()
  message(STATUS "BUILD_WITH_LLAMA=OFF - Building without bundled LLM (lightweight build)")
  message(STATUS "  Use external LLM APIs (OpenAI, etc.) for AI features")
endif()

# ============================================================================

# CEF path - platform-specific
if(OS_MACOS)
  set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_macos")
  set(LLAMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama_macos")
elseif(OS_LINUX)
  set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_linux")
  set(LLAMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama_linux")
elseif(OS_WINDOWS)
  set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_windows")
  set(LLAMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama_windows")
endif()

# Find CEF
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# Find OpenSSL for HTTPS server (.owl domains)
find_package(OpenSSL REQUIRED)

# Add CEF config
add_subdirectory(${CEF_ROOT} libcef_dll_wrapper)

# Include directories - add all module subdirectories
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/core
  ${CMAKE_CURRENT_SOURCE_DIR}/include/automation
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ai
  ${CMAKE_CURRENT_SOURCE_DIR}/include/captcha
  ${CMAKE_CURRENT_SOURCE_DIR}/include/content
  ${CMAKE_CURRENT_SOURCE_DIR}/include/stealth
  ${CMAKE_CURRENT_SOURCE_DIR}/include/stealth/spoofs
  ${CMAKE_CURRENT_SOURCE_DIR}/include/stealth/workers
  ${CMAKE_CURRENT_SOURCE_DIR}/include/network
  ${CMAKE_CURRENT_SOURCE_DIR}/include/profile
  ${CMAKE_CURRENT_SOURCE_DIR}/include/media
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/include/util
  ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
  ${CMAKE_CURRENT_SOURCE_DIR}/include/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gpu
  ${CEF_ROOT}
)

# Common source files (cross-platform, excluding main entry points)
# Organized by module for better maintainability
set(OWL_COMMON_SRCS
  # Core - Browser fundamentals
  src/core/owl_app.cc
  src/core/owl_browser_manager.cc
  src/core/owl_action_verifier.cc
  src/core/owl_client.cc
  src/core/owl_request_context_handler.cc
  src/core/owl_ipc_server.cc
  src/core/owl_network_interceptor.cc
  src/core/owl_console_logger.cc
  src/core/owl_download_handler.cc
  src/core/owl_dialog_handler.cc
  src/core/owl_tab_manager.cc

  # Automation - Element interaction and scanning
  src/automation/owl_automation_handler.cc
  src/automation/owl_render_tracker.cc
  src/automation/owl_element_scanner.cc
  src/automation/owl_dom_visitor.cc

  # AI - LLM integration and intelligence
  src/ai/owl_ai_intelligence.cc
  src/ai/owl_semantic_matcher.cc
  src/ai/owl_llama_server.cc
  src/ai/owl_llm_client.cc
  src/ai/owl_llm_guardrail.cc
  src/ai/owl_nla.cc
  src/ai/owl_query_router.cc
  src/ai/owl_pii_scrubber.cc

  # AI - Enhanced semantic scoring (multi-scorer ensemble for 90%+ accuracy)
  src/ai/owl_text_similarity_scorer.cc
  src/ai/owl_visual_proximity_scorer.cc
  src/ai/owl_contextual_relevance_scorer.cc
  src/ai/owl_element_type_scorer.cc
  src/ai/owl_composite_scorer.cc

  # Captcha - Detection and solving
  src/captcha/owl_captcha_detector.cc
  src/captcha/owl_firewall_detector.cc
  src/captcha/owl_captcha_classifier.cc
  src/captcha/owl_captcha_utils.cc
  src/captcha/owl_text_captcha_solver.cc
  src/captcha/owl_image_captcha_solver.cc
  src/captcha/owl_image_captcha_provider_base.cc
  src/captcha/owl_image_captcha_provider_owl.cc
  src/captcha/owl_image_captcha_provider_recaptcha.cc
  src/captcha/owl_image_captcha_provider_cloudflare.cc
  src/captcha/owl_image_captcha_factory.cc

  # Content - Extraction and conversion
  src/content/owl_content_extractor.cc
  src/content/owl_markdown_converter.cc
  src/content/owl_extraction_template.cc
  src/content/owl_json_extractor.cc

  # Stealth - Anti-detection
  src/stealth/owl_stealth.cc
  src/stealth/owl_virtual_machine.cc
  src/stealth/owl_fingerprint_generator.cc
  src/stealth/owl_seed_api.cc
  src/stealth/owl_resource_blocker.cc
  src/stealth/owl_font_spoofer.cc
  src/stealth/owl_spoof_manager.cc

  # Stealth - Worker patchers
  src/stealth/workers/owl_worker_patcher.cc

  # Stealth - Spoof modules
  src/stealth/spoofs/spoof_utils.cc
  src/stealth/spoofs/navigator_spoof.cc
  src/stealth/spoofs/canvas_spoof.cc
  src/stealth/spoofs/webgl_spoof.cc
  src/stealth/spoofs/audio_spoof.cc
  src/stealth/spoofs/screen_spoof.cc
  src/stealth/spoofs/timezone_spoof.cc

  # GPU - GPU Virtualization for anti-fingerprinting
  src/gpu/owl_gpu_virtualization.cc
  src/gpu/owl_gl_hooks.cc
  src/gpu/owl_gpu_api.cc
  src/gpu/profile/owl_gpu_profile.cc
  src/gpu/context/owl_gpu_context.cc
  src/gpu/interception/owl_gl_interceptor.cc
  src/gpu/translation/owl_shader_translator.cc
  src/gpu/normalization/owl_render_normalizer.cc
  src/gpu/normalization/owl_timing_normalizer.cc

  # Network - Proxy, cookies, demographics
  src/network/owl_proxy_manager.cc
  src/network/owl_cookie_manager.cc
  src/network/owl_demographics.cc

  # Profile - Browser fingerprinting
  src/profile/owl_browser_profile.cc

  # Media - Video recording and live streaming
  src/media/owl_video_recorder.cc
  src/media/owl_live_streamer.cc
  src/media/shared_frame_buffer.c

  # UI - Shared UI components
  src/ui/owl_task_state.cc
  src/ui/owl_homepage.cc
  src/ui/owl_playground.cc

  # Util - Utilities
  src/util/owl_platform_utils.cc
  src/util/owl_license.cc
  src/util/owl_test_scheme_handler.cc
  src/util/owl_https_server.cc
  src/util/logger.cc
  src/util/owl_thread_pool.cc
  src/util/owl_context_pool.cc

  # Utils - Image processing
  src/utils/owl_image_enhancer.cc

  # Utils - Virtual Camera
  src/utils/owl_virtual_camera.cc
)

# Source files for headless mode (common + subprocess IPC main)
set(OWL_SRCS
  src/core/owl_subprocess.cc
  ${OWL_COMMON_SRCS}
)

# Add platform-specific sources for headless mode
if(OS_MACOS)
  list(APPEND OWL_SRCS
    src/core/owl_macos_init.mm  # Headless NSApplication with CefAppProtocol
    src/ui/owl_dev_console.mm
    src/ui/owl_dev_elements.mm
    src/ui/owl_dev_network.mm
    src/util/fishhook.c  # macOS-only dynamic function hooking
  )
elseif(OS_LINUX)
  list(APPEND OWL_SRCS
    src/ui/owl_dev_console_linux.cc
    src/ui/owl_dev_elements_linux.cc
    src/ui/owl_dev_network_linux.cc
  )
endif()

# Header files - organized by module
set(OWL_HEADERS
  # Core
  include/core/owl_app.h
  include/core/owl_browser_manager.h
  include/core/owl_client.h

  # Automation
  include/automation/owl_automation_handler.h
  include/automation/owl_element_scanner.h
  include/automation/owl_dom_visitor.h
  include/automation/owl_render_tracker.h

  # AI
  include/ai/owl_ai_intelligence.h
  include/ai/owl_semantic_matcher.h
  include/ai/owl_llama_server.h
  include/ai/owl_llm_client.h
  include/ai/owl_llm_guardrail.h
  include/ai/owl_nla.h
  include/ai/owl_query_router.h
  include/ai/owl_pii_scrubber.h

  # AI - Enhanced semantic scoring headers
  include/ai/owl_text_similarity_scorer.h
  include/ai/owl_visual_proximity_scorer.h
  include/ai/owl_contextual_relevance_scorer.h
  include/ai/owl_element_type_scorer.h
  include/ai/owl_composite_scorer.h

  # Captcha
  include/captcha/owl_captcha_detector.h
  include/captcha/owl_captcha_classifier.h
  include/captcha/owl_captcha_utils.h
  include/captcha/owl_text_captcha_solver.h
  include/captcha/owl_image_captcha_solver.h
  include/captcha/owl_image_captcha_provider.h
  include/captcha/owl_image_captcha_provider_base.h
  include/captcha/owl_image_captcha_provider_owl.h
  include/captcha/owl_image_captcha_provider_recaptcha.h
  include/captcha/owl_image_captcha_provider_cloudflare.h
  include/captcha/owl_image_captcha_factory.h

  # Content
  include/content/owl_content_extractor.h
  include/content/owl_markdown_converter.h
  include/content/owl_extraction_template.h
  include/content/owl_json_extractor.h

  # Stealth
  include/stealth/owl_stealth.h
  include/stealth/owl_virtual_machine.h
  include/stealth/owl_resource_blocker.h
  include/stealth/owl_spoof_manager.h

  # Stealth - Worker patcher headers
  include/stealth/workers/owl_worker_patcher.h

  # Stealth - Spoof module headers
  include/stealth/spoofs/spoof_utils.h
  include/stealth/spoofs/navigator_spoof.h
  include/stealth/spoofs/canvas_spoof.h
  include/stealth/spoofs/webgl_spoof.h
  include/stealth/spoofs/audio_spoof.h
  include/stealth/spoofs/screen_spoof.h
  include/stealth/spoofs/timezone_spoof.h

  # GPU - GPU Virtualization
  include/gpu/owl_gpu_virtualization.h
  include/gpu/owl_gpu_profile.h
  include/gpu/owl_gpu_context.h
  include/gpu/owl_gl_interceptor.h
  include/gpu/owl_shader_translator.h
  include/gpu/owl_render_normalizer.h
  include/gpu/owl_timing_normalizer.h

  # Network
  include/network/owl_proxy_manager.h
  include/network/owl_cookie_manager.h
  include/network/owl_demographics.h

  # Profile
  include/profile/owl_browser_profile.h

  # Media
  include/media/owl_video_recorder.h
  include/media/owl_live_streamer.h

  # UI
  include/ui/owl_homepage.h
  include/ui/owl_playground.h
  include/ui/owl_dev_console.h
  include/ui/owl_dev_elements.h
  include/ui/owl_dev_network.h
  include/ui/owl_task_state.h

  # Util
  include/util/owl_platform_utils.h
  include/util/owl_license.h
  include/util/owl_test_scheme_handler.h
  include/util/logger.h
  include/util/owl_thread_pool.h
  include/util/owl_context_pool.h
)

# UI-specific source files (common + UI-specific)
set(OWL_UI_SRCS
  ${OWL_COMMON_SRCS}
  src/ui/owl_ui_browser.cc
  src/ui/owl_agent_controller.cc
)

# UI-specific headers
set(OWL_UI_HEADERS
  include/ui/owl_ui_browser.h
  include/ui/owl_ui_delegate.h
  include/ui/owl_ui_toolbar.h
  include/ui/owl_agent_controller.h
  resources/icons/icons.h
)

# Add platform-specific UI sources
if(OS_MACOS)
  list(APPEND OWL_UI_SRCS
    src/ui/owl_ui_main.mm
    src/ui/owl_ui_delegate.mm
    src/ui/owl_ui_toolbar.mm
    src/ui/owl_playground_window.mm
    src/media/owl_native_screenshot.mm
    src/ui/owl_dev_console.mm
    src/ui/owl_dev_elements.mm
    src/ui/owl_dev_network.mm
    src/util/fishhook.c  # macOS-only dynamic function hooking
  )
  list(APPEND OWL_HEADERS
    include/ui/owl_playground_window.h
    include/media/owl_native_screenshot.h
  )
  message(STATUS "Added macOS UI sources")
elseif(OS_LINUX)
  list(APPEND OWL_UI_SRCS
    src/ui/owl_ui_main_linux.cc
    src/ui/owl_ui_delegate_linux.cc
    src/ui/owl_ui_toolbar_linux.cc
    src/ui/owl_playground_window_linux.cc
    src/media/owl_native_screenshot_linux.cc
    src/ui/owl_dev_console_linux.cc
    src/ui/owl_dev_elements_linux.cc
    src/ui/owl_dev_network_linux.cc
  )
  list(APPEND OWL_HEADERS
    include/ui/owl_playground_window.h
    include/media/owl_native_screenshot.h
  )
  message(STATUS "Added Linux UI sources (stub implementations)")
endif()

# Subprocess executable for IPC (reuse headless sources)
set(SUBPROCESS_SRCS ${OWL_SRCS})

# Add platform-specific sources for subprocess
if(OS_MACOS)
  list(APPEND SUBPROCESS_SRCS
    src/ui/owl_dev_console.mm
    src/ui/owl_dev_elements.mm
    src/ui/owl_dev_network.mm
  )
elseif(OS_LINUX)
  list(APPEND SUBPROCESS_SRCS
    src/ui/owl_dev_console_linux.cc
    src/ui/owl_dev_elements_linux.cc
    src/ui/owl_dev_network_linux.cc
  )
endif()

# Find libmaxminddb
if(OS_MACOS)
  find_library(MAXMINDDB_LIBRARY maxminddb PATHS /opt/homebrew/lib /usr/local/lib)
  find_path(MAXMINDDB_INCLUDE_DIR maxminddb.h PATHS /opt/homebrew/include /usr/local/include)
elseif(OS_LINUX)
  find_library(MAXMINDDB_LIBRARY maxminddb PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
  find_path(MAXMINDDB_INCLUDE_DIR maxminddb.h PATHS /usr/include /usr/local/include)
endif()

if(NOT MAXMINDDB_LIBRARY OR NOT MAXMINDDB_INCLUDE_DIR)
  if(OS_MACOS)
    message(WARNING "libmaxminddb not found. Install with: brew install libmaxminddb")
  elseif(OS_LINUX)
    message(WARNING "libmaxminddb not found. Install with: sudo apt-get install libmaxminddb-dev")
  endif()
else()
  message(STATUS "Found libmaxminddb: ${MAXMINDDB_LIBRARY}")
  include_directories(${MAXMINDDB_INCLUDE_DIR})
endif()

# Find SQLCipher for encrypted VM profiles database
if(OS_MACOS)
  find_library(SQLCIPHER_LIBRARY sqlcipher PATHS /opt/homebrew/opt/sqlcipher/lib /usr/local/opt/sqlcipher/lib NO_DEFAULT_PATH)
  find_path(SQLCIPHER_INCLUDE_DIR sqlcipher/sqlite3.h PATHS /opt/homebrew/opt/sqlcipher/include /usr/local/opt/sqlcipher/include NO_DEFAULT_PATH)
elseif(OS_LINUX)
  find_library(SQLCIPHER_LIBRARY sqlcipher PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
  find_path(SQLCIPHER_INCLUDE_DIR sqlcipher/sqlite3.h PATHS /usr/include /usr/local/include)
endif()

if(NOT SQLCIPHER_LIBRARY OR NOT SQLCIPHER_INCLUDE_DIR)
  if(OS_MACOS)
    message(WARNING "SQLCipher not found. Install with: brew install sqlcipher")
  elseif(OS_LINUX)
    message(WARNING "SQLCipher not found. Install with: sudo apt-get install libsqlcipher-dev")
  endif()
  message(WARNING "VM profiles database will not be encrypted!")
else()
  message(STATUS "Found SQLCipher: ${SQLCIPHER_LIBRARY}")
  # Add SQLCipher include BEFORE other homebrew includes to avoid conflicts with system SQLite
  include_directories(BEFORE ${SQLCIPHER_INCLUDE_DIR})
  add_compile_definitions(OWL_USE_SQLCIPHER=1)
endif()

# VM Profiles database path
set(VM_PROFILES_DB "${CMAKE_CURRENT_SOURCE_DIR}/data/profiles/vm_profiles.db")
if(EXISTS "${VM_PROFILES_DB}")
  message(STATUS "Found VM profiles database: ${VM_PROFILES_DB}")
else()
  message(WARNING "VM profiles database not found. Run: npm run build:profiles")
endif()

if(OS_MACOS)
  # Create helper sources - using modular paths
  set(OWL_HELPER_SRCS_MAC
    # Core
    src/core/owl_subprocess.cc
    src/core/owl_macos_init.mm  # Headless NSApplication with CefAppProtocol
    src/core/owl_app.cc
    src/core/owl_browser_manager.cc
    src/core/owl_action_verifier.cc
    src/core/owl_client.cc
    src/core/owl_request_context_handler.cc
    src/core/owl_ipc_server.cc  # Multi-IPC support (Unix domain sockets)
    src/core/owl_network_interceptor.cc
    src/core/owl_console_logger.cc
    src/core/owl_download_handler.cc
    src/core/owl_dialog_handler.cc
    src/core/owl_tab_manager.cc

    # Automation
    src/automation/owl_automation_handler.cc
    src/automation/owl_render_tracker.cc
    src/automation/owl_element_scanner.cc
    src/automation/owl_dom_visitor.cc

    # AI
    src/ai/owl_ai_intelligence.cc
    src/ai/owl_semantic_matcher.cc
    src/ai/owl_llama_server.cc
    src/ai/owl_llm_client.cc
    src/ai/owl_llm_guardrail.cc
    src/ai/owl_nla.cc
    src/ai/owl_query_router.cc
    src/ai/owl_pii_scrubber.cc

    # AI - Enhanced semantic scoring
    src/ai/owl_text_similarity_scorer.cc
    src/ai/owl_visual_proximity_scorer.cc
    src/ai/owl_contextual_relevance_scorer.cc
    src/ai/owl_element_type_scorer.cc
    src/ai/owl_composite_scorer.cc

    # Captcha
    src/captcha/owl_captcha_detector.cc
    src/captcha/owl_firewall_detector.cc
    src/captcha/owl_captcha_classifier.cc
    src/captcha/owl_captcha_utils.cc
    src/captcha/owl_text_captcha_solver.cc
    src/captcha/owl_image_captcha_solver.cc
    src/captcha/owl_image_captcha_provider_base.cc
    src/captcha/owl_image_captcha_provider_owl.cc
    src/captcha/owl_image_captcha_provider_recaptcha.cc
    src/captcha/owl_image_captcha_provider_cloudflare.cc
    src/captcha/owl_image_captcha_factory.cc

    # Content
    src/content/owl_content_extractor.cc
    src/content/owl_markdown_converter.cc
    src/content/owl_extraction_template.cc
    src/content/owl_json_extractor.cc

    # Stealth
    src/stealth/owl_stealth.cc
    src/stealth/owl_virtual_machine.cc
    src/stealth/owl_fingerprint_generator.cc
    src/stealth/owl_seed_api.cc
    src/stealth/owl_resource_blocker.cc
    src/stealth/owl_font_spoofer.cc
    src/stealth/owl_spoof_manager.cc

    # Stealth - Worker patchers
    src/stealth/workers/owl_worker_patcher.cc

    # Stealth - Spoof modules
    src/stealth/spoofs/spoof_utils.cc
    src/stealth/spoofs/navigator_spoof.cc
    src/stealth/spoofs/canvas_spoof.cc
    src/stealth/spoofs/webgl_spoof.cc
    src/stealth/spoofs/audio_spoof.cc
    src/stealth/spoofs/screen_spoof.cc
    src/stealth/spoofs/timezone_spoof.cc

    # GPU - GPU Virtualization for anti-fingerprinting
    src/gpu/owl_gpu_virtualization.cc
    src/gpu/owl_gl_hooks.cc
    src/gpu/owl_gpu_api.cc
    src/gpu/profile/owl_gpu_profile.cc
    src/gpu/context/owl_gpu_context.cc
    src/gpu/interception/owl_gl_interceptor.cc
    src/gpu/translation/owl_shader_translator.cc
    src/gpu/normalization/owl_render_normalizer.cc
    src/gpu/normalization/owl_timing_normalizer.cc

    # Network
    src/network/owl_proxy_manager.cc
    src/network/owl_cookie_manager.cc
    src/network/owl_demographics.cc

    # Profile
    src/profile/owl_browser_profile.cc

    # Media
    src/media/owl_video_recorder.cc
    src/media/owl_live_streamer.cc
    src/media/shared_frame_buffer.c

    # UI (shared components)
    src/ui/owl_task_state.cc
    src/ui/owl_homepage.cc
    src/ui/owl_playground.cc
    src/ui/owl_dev_console.mm
    src/ui/owl_dev_elements.mm
    src/ui/owl_dev_network.mm

    # Util
    src/util/owl_platform_utils.cc
    src/util/owl_license.cc
    src/util/owl_test_scheme_handler.cc
    src/util/owl_https_server.cc
    src/util/logger.cc
    src/util/owl_thread_pool.cc
    src/util/owl_context_pool.cc
    src/util/fishhook.c  # macOS-only dynamic function hooking (required by owl_gl_hooks)
    src/utils/owl_image_enhancer.cc
    src/utils/owl_virtual_camera.cc
  )

  set(CEF_TARGET "owl_browser")
  set(CEF_HELPER_TARGET "owl_browser_Helper")
  set(CEF_HELPER_OUTPUT_NAME "owl_browser Helper")

  SET_CEF_TARGET_OUT_DIR()
  set(CEF_APP "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app")

  set(EXECUTABLE_NAME "${CEF_TARGET}")
  set(PRODUCT_NAME "${CEF_TARGET}")

  # Create Helper app bundles first
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
    set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")

    add_executable(${_helper_target} MACOSX_BUNDLE ${OWL_HELPER_SRCS_MAC})
    SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
    add_dependencies(${_helper_target} libcef_dll_wrapper)
    target_link_libraries(${_helper_target} libcef_dll_wrapper ${CEF_STANDARD_LIBS} z curl ${MAXMINDDB_LIBRARY} ${SQLCIPHER_LIBRARY}
      OpenSSL::SSL OpenSSL::Crypto "-framework IOKit" "-framework Security" "-framework CoreFoundation")
    set_target_properties(${_helper_target} PROPERTIES
      OUTPUT_NAME ${_helper_output_name}
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Helper-Info.plist"
      MACOSX_BUNDLE_BUNDLE_NAME "owl_browser Helper${_name_suffix}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.olib.owlbrowser.helper${_target_suffix}"
      MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )
  endforeach()

  # Main app bundle - depends on all helpers
  add_executable(${CEF_TARGET} MACOSX_BUNDLE ${OWL_SRCS} ${OWL_HEADERS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
  add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
  target_link_libraries(${CEF_TARGET} libcef_dll_wrapper ${CEF_STANDARD_LIBS} z curl ${MAXMINDDB_LIBRARY} ${SQLCIPHER_LIBRARY}
    OpenSSL::SSL OpenSSL::Crypto "-framework IOKit" "-framework Security" "-framework CoreFoundation")

  # Make main app depend on all helpers
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 1 _target_suffix)
    set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
    add_dependencies(${CEF_TARGET} "${_helper_target}")
  endforeach()

  # Copy CEF framework to main app
  COPY_MAC_FRAMEWORK("${CEF_TARGET}" "${CEF_BINARY_DIR}" "${CEF_APP}")

  # Copy helpers and CEF framework to each helper (runs after main app is built)
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
    set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")

    # Copy helper to main app's Frameworks
    add_custom_command(
      TARGET ${CEF_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CEF_TARGET_OUT_DIR}/${_helper_output_name}.app"
              "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM
    )

    # Create symlink to CEF framework in helper (lightweight alternative to copying)
    add_custom_command(
      TARGET ${CEF_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app/Contents/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "../../../Chromium Embedded Framework.framework"
              "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app/Contents/Frameworks/Chromium Embedded Framework.framework"
      VERBATIM
    )
  endforeach()
endif()

if(OS_LINUX)
  # Linux executable (no app bundle)
  set(CEF_TARGET "owl_browser")

  add_executable(${CEF_TARGET} ${OWL_SRCS} ${OWL_HEADERS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
  add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
  target_link_libraries(${CEF_TARGET}
    libcef_dll_wrapper
    ${CEF_ROOT}/Release/libcef.so
    ${CEF_STANDARD_LIBS}
    z
    curl
    ${MAXMINDDB_LIBRARY}
    ${SQLCIPHER_LIBRARY}
    crypto
    ssl
  )

  # CRITICAL: Set RPATH to use $ORIGIN so executable finds libraries relative to itself
  # This allows running the browser from any directory
  set_target_properties(${CEF_TARGET} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
  )

  # Copy CEF files to output directory
  add_custom_command(
    TARGET ${CEF_TARGET}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Release"
            "$<TARGET_FILE_DIR:${CEF_TARGET}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources"
            "$<TARGET_FILE_DIR:${CEF_TARGET}>"
    VERBATIM
  )

  # Copy llama-server binary (only if BUILD_WITH_LLAMA is ON)
  set(LLAMA_BIN_DIR "${LLAMA_ROOT}/bin")
  set(LLAMA_SERVER_BIN "${LLAMA_BIN_DIR}/llama-server")

  if(BUILD_WITH_LLAMA)
    if(EXISTS "${LLAMA_SERVER_BIN}")
      message(STATUS "Found llama-server: ${LLAMA_SERVER_BIN}")
      # Define BUILD_WITH_LLAMA=1 so code knows LLM is available
      target_compile_definitions(${CEF_TARGET} PRIVATE BUILD_WITH_LLAMA=1)
      # Also define for helper targets that compile owl_browser_manager.cc
      foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
        string(REPLACE ":" ";" _suffix_list ${_suffix_list})
        list(GET _suffix_list 1 _target_suffix)
        set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
        target_compile_definitions(${_helper_target} PRIVATE BUILD_WITH_LLAMA=1)
      endforeach()
      add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${LLAMA_SERVER_BIN}"
          "$<TARGET_FILE_DIR:${CEF_TARGET}>/llama-server"
        COMMAND chmod +x
          "$<TARGET_FILE_DIR:${CEF_TARGET}>/llama-server"
        COMMENT "Copying llama-server binary"
      )

      # Copy shared libraries
      file(GLOB LLAMA_LIBS "${LLAMA_BIN_DIR}/*.so*")
      if(LLAMA_LIBS)
        foreach(LIB ${LLAMA_LIBS})
          add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
              "${LIB}"
              "$<TARGET_FILE_DIR:${CEF_TARGET}>/"
            COMMENT "Copying llama library: ${LIB}"
          )
        endforeach()
      endif()
    else()
      message(WARNING "llama-server not found at: ${LLAMA_SERVER_BIN}")
      message(WARNING "Run 'npm run download:llama' to download llama.cpp binaries")
    endif()

    # Copy models
    set(MODEL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/llm-assist.gguf")
    if(EXISTS "${MODEL_FILE}")
      file(SIZE "${MODEL_FILE}" MODEL_SIZE)
      math(EXPR MODEL_SIZE_MB "${MODEL_SIZE} / 1048576")
      message(STATUS "Found model: ${MODEL_FILE} (${MODEL_SIZE_MB} MB)")

      add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CEF_TARGET}>/models"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${MODEL_FILE}"
          "$<TARGET_FILE_DIR:${CEF_TARGET}>/models/llm-assist.gguf"
        COMMENT "Copying LLM model (${MODEL_SIZE_MB} MB)"
      )
    else()
      message(WARNING "Model not found at: ${MODEL_FILE}")
    endif()

    # Copy mmproj
    set(MMPROJ_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/mmproj-llm-assist.gguf")
    if(EXISTS "${MMPROJ_FILE}")
      add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${MMPROJ_FILE}"
          "$<TARGET_FILE_DIR:${CEF_TARGET}>/models/mmproj-llm-assist.gguf"
        COMMENT "Copying mmproj file"
      )
    endif()
  else()
    message(STATUS "Skipping llama-server and models (BUILD_WITH_LLAMA=OFF)")
  endif()

  # Copy GeoLite2 database
  set(GEOLITE2_FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GeoLite2-City.mmdb")
  if(EXISTS "${GEOLITE2_FILE}")
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/third_party"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${GEOLITE2_FILE}"
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/third_party/GeoLite2-City.mmdb"
      COMMENT "Copying GeoLite2 database"
    )
  endif()

  # Copy statics folder (includes assets/gestures for virtual camera)
  set(STATICS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/statics")
  if(EXISTS "${STATICS_DIR}")
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${STATICS_DIR}"
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/statics"
      COMMENT "Copying statics folder"
    )
  endif()

  # Copy VM profiles database
  if(EXISTS "${VM_PROFILES_DB}")
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/data/profiles"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${VM_PROFILES_DB}"
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/data/profiles/vm_profiles.db"
      COMMENT "Copying VM profiles database"
    )
  endif()

  # Copy app resources (logo, icon, etc.) for Linux
  set(LOGO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/Olib-owl.apng")
  if(EXISTS "${LOGO_FILE}")
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
        "${LOGO_FILE}"
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/Olib-owl.apng"
      COMMENT "Copying Olib-owl.apng"
    )
  endif()

  set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png")
  if(EXISTS "${ICON_FILE}")
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
        "${ICON_FILE}"
        "$<TARGET_FILE_DIR:${CEF_TARGET}>/icon.png"
      COMMENT "Copying icon.png"
    )
  endif()
endif()

# macOS specific settings
if(OS_MACOS)
  set_target_properties(owl_browser PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  )

  # Copy custom Info.plist template to hide from Dock
  set(INFO_PLIST_FILE "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Info.plist")
  add_custom_command(TARGET owl_browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.template"
      "${INFO_PLIST_FILE}"
    COMMENT "Copying Info.plist to app bundle to hide from Dock"
  )

  # Copy app icon and logo
  set(ICON_FILE "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/icon.icns")
  add_custom_command(TARGET owl_browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns"
      "${ICON_FILE}"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/Olib-owl.apng"
      "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/Olib-owl.apng"
    COMMENT "Copying app icon and logo to bundle"
  )

  # Copy llama-server binary and dependencies to app bundle (only if BUILD_WITH_LLAMA is ON)
  set(LLAMA_BIN_DIR "${LLAMA_ROOT}/bin")
  set(LLAMA_SERVER_BIN "${LLAMA_BIN_DIR}/llama-server")

  if(BUILD_WITH_LLAMA)
    if(EXISTS "${LLAMA_SERVER_BIN}")
      message(STATUS "Found llama-server: ${LLAMA_SERVER_BIN}")
      # Define BUILD_WITH_LLAMA=1 so code knows LLM is available
      target_compile_definitions(owl_browser PRIVATE BUILD_WITH_LLAMA=1)
      # Also define for helper targets
      foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
        string(REPLACE ":" ";" _suffix_list ${_suffix_list})
        list(GET _suffix_list 1 _target_suffix)
        set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
        target_compile_definitions(${_helper_target} PRIVATE BUILD_WITH_LLAMA=1)
      endforeach()

      # Copy llama-server executable to MacOS directory (same as main executable)
      add_custom_command(TARGET owl_browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${LLAMA_SERVER_BIN}"
          "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/MacOS/llama-server"
        COMMAND chmod +x
          "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/MacOS/llama-server"
        COMMENT "Copying llama-server binary to app bundle"
      )

      # Copy llama.cpp shared libraries (*.dylib) to MacOS directory
      file(GLOB LLAMA_DYLIBS "${LLAMA_BIN_DIR}/*.dylib")
      if(LLAMA_DYLIBS)
        foreach(DYLIB ${LLAMA_DYLIBS})
          add_custom_command(TARGET owl_browser POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
              "${DYLIB}"
              "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/MacOS/"
            COMMENT "Copying llama dylib: ${DYLIB}"
          )
        endforeach()
      endif()

      # Copy ggml-metal.metal shader file (required for Metal acceleration on macOS)
      set(METAL_SHADER "${LLAMA_BIN_DIR}/ggml-metal.metal")
      if(EXISTS "${METAL_SHADER}")
        add_custom_command(TARGET owl_browser POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy
            "${METAL_SHADER}"
            "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/MacOS/ggml-metal.metal"
          COMMENT "Copying Metal shader for GPU acceleration"
        )
      endif()

      message(STATUS "llama-server will be bundled with app at: ${CEF_TARGET}.app/Contents/MacOS/")
    else()
      message(WARNING "llama-server not found at: ${LLAMA_SERVER_BIN}")
      message(WARNING "Run 'npm run download:llama' to download llama.cpp binaries")
    endif()

    # Copy LLM model to app bundle Resources
    set(MODEL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/llm-assist.gguf")

    if(EXISTS "${MODEL_FILE}")
      # Get model file size for logging
      file(SIZE "${MODEL_FILE}" MODEL_SIZE)
      math(EXPR MODEL_SIZE_MB "${MODEL_SIZE} / 1048576")
      message(STATUS "Found model: ${MODEL_FILE} (${MODEL_SIZE_MB} MB)")

      # Create models directory and copy model
      add_custom_command(TARGET owl_browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/models"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${MODEL_FILE}"
          "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/models/llm-assist.gguf"
        COMMENT "Copying LLM model (${MODEL_SIZE_MB} MB) to app bundle"
      )

      message(STATUS "Model will be bundled at: ${CEF_TARGET}.app/Contents/Resources/models/llm-assist.gguf")
    else()
      message(WARNING "Model not found at: ${MODEL_FILE}")
      message(WARNING "Place your GGUF model at: models/llm-assist.gguf")
    endif()

    # Bundle mmproj file for vision model support
    set(MMPROJ_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/mmproj-llm-assist.gguf")

    if(EXISTS "${MMPROJ_FILE}")
      # Get mmproj file size for logging
      file(SIZE "${MMPROJ_FILE}" MMPROJ_SIZE)
      math(EXPR MMPROJ_SIZE_MB "${MMPROJ_SIZE} / 1048576")
      message(STATUS "Found mmproj: ${MMPROJ_FILE} (${MMPROJ_SIZE_MB} MB)")

      # Copy mmproj to models directory
      add_custom_command(TARGET owl_browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${MMPROJ_FILE}"
          "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/models/mmproj-llm-assist.gguf"
        COMMENT "Copying mmproj file (${MMPROJ_SIZE_MB} MB) to app bundle"
      )

      message(STATUS "Mmproj will be bundled at: ${CEF_TARGET}.app/Contents/Resources/models/mmproj-llm-assist.gguf")
    else()
      message(WARNING "Mmproj not found at: ${MMPROJ_FILE}")
      message(WARNING "Vision model features will not work without mmproj file")
      message(WARNING "Place your mmproj file at: models/mmproj-llm-assist.gguf")
    endif()
  else()
    message(STATUS "Skipping llama-server and models for headless build (BUILD_WITH_LLAMA=OFF)")
  endif()

  # Copy GeoLite2 database to app bundle Resources for demographics/geolocation
  set(GEOLITE2_FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GeoLite2-City.mmdb")

  if(EXISTS "${GEOLITE2_FILE}")
    add_custom_command(TARGET owl_browser POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/third_party"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${GEOLITE2_FILE}"
        "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/third_party/GeoLite2-City.mmdb"
      COMMENT "Copying GeoLite2 database to app bundle"
    )
    message(STATUS "GeoLite2 database will be bundled at: ${CEF_TARGET}.app/Contents/Resources/third_party/GeoLite2-City.mmdb")
  else()
    message(WARNING "GeoLite2 database not found at: ${GEOLITE2_FILE}")
    message(WARNING "Demographics/geolocation features will not work")
  endif()

  # Copy statics folder to app bundle Resources for owl:// URL scheme
  # CRITICAL: Using custom target that ALWAYS runs to ensure statics are synced
  # POST_BUILD only runs when target is rebuilt, but we need statics synced every time
  set(STATICS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/statics")

  if(EXISTS "${STATICS_DIR}")
    # Create an always-run custom target for syncing statics
    add_custom_target(sync_statics_macos ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${STATICS_DIR}"
        "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/statics"
      COMMENT "Syncing statics folder to app bundle"
    )
    # Make owl_browser depend on sync_statics so statics are always synced when building
    add_dependencies(owl_browser sync_statics_macos)
    message(STATUS "Statics folder will be bundled at: ${CEF_TARGET}.app/Contents/Resources/statics")
  else()
    message(WARNING "Statics folder not found at: ${STATICS_DIR}")
  endif()

  # Copy VM profiles database to app bundle Resources
  if(EXISTS "${VM_PROFILES_DB}")
    add_custom_command(TARGET owl_browser POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/data/profiles"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${VM_PROFILES_DB}"
        "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app/Contents/Resources/data/profiles/vm_profiles.db"
      COMMENT "Copying VM profiles database to app bundle"
    )
    message(STATUS "VM profiles will be bundled at: ${CEF_TARGET}.app/Contents/Resources/data/profiles/vm_profiles.db")
  endif()

  # Cleanup: Remove ALL helper apps from Release folder (they are inside the .app bundles)
  # This includes both headless helpers and any leftover UI helpers
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    # Clean headless helpers
    set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")
    add_custom_command(TARGET owl_browser POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CEF_TARGET_OUT_DIR}/${_helper_output_name}.app"
      COMMENT "Cleaning up ${_helper_output_name}.app from Release folder"
      VERBATIM
    )
    # Clean UI helpers (in case they exist from previous builds)
    set(_ui_helper_output_name "owl_browser_ui Helper${_name_suffix}")
    add_custom_command(TARGET owl_browser POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CEF_TARGET_OUT_DIR}/${_ui_helper_output_name}.app"
      COMMENT "Cleaning up ${_ui_helper_output_name}.app from Release folder"
      VERBATIM
    )
  endforeach()
  # Also clean up any leftover UI app bundle
  add_custom_command(TARGET owl_browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      "${CEF_TARGET_OUT_DIR}/owl_browser_ui.app"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      "${CEF_TARGET_OUT_DIR}/Owl Browser.app"
    COMMENT "Cleaning up UI app bundles from Release folder"
    VERBATIM
  )

  # ============================================================================
  # UI VERSION BUILD TARGET (with dock icon and UI)
  # ============================================================================

  set(CEF_UI_TARGET "owl_browser_ui")
  set(CEF_UI_HELPER_TARGET "owl_browser_ui_Helper")
  set(CEF_UI_HELPER_OUTPUT_NAME "owl_browser_ui Helper")

  set(CEF_UI_APP "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app")

  # UI helpers need native screenshot support
  set(OWL_UI_HELPER_SRCS_MAC ${OWL_HELPER_SRCS_MAC})
  list(APPEND OWL_UI_HELPER_SRCS_MAC src/media/owl_native_screenshot.mm)

  # Create Helper app bundles for UI version
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_ui_helper_target "${CEF_UI_HELPER_TARGET}${_target_suffix}")
    set(_ui_helper_output_name "${CEF_UI_HELPER_OUTPUT_NAME}${_name_suffix}")

    add_executable(${_ui_helper_target} MACOSX_BUNDLE ${OWL_UI_HELPER_SRCS_MAC})
    SET_EXECUTABLE_TARGET_PROPERTIES(${_ui_helper_target})
    add_dependencies(${_ui_helper_target} libcef_dll_wrapper)
    target_link_libraries(${_ui_helper_target} libcef_dll_wrapper ${CEF_STANDARD_LIBS} z curl ${MAXMINDDB_LIBRARY} ${SQLCIPHER_LIBRARY}
      OpenSSL::SSL OpenSSL::Crypto "-framework IOKit" "-framework Security" "-framework CoreFoundation")
    target_compile_definitions(${_ui_helper_target} PRIVATE BUILD_UI=1)
    set_target_properties(${_ui_helper_target} PROPERTIES
      OUTPUT_NAME ${_ui_helper_output_name}
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Helper-Info.plist"
      MACOSX_BUNDLE_BUNDLE_NAME "owl_browser_ui Helper${_name_suffix}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.olib.owlbrowser.ui.helper${_target_suffix}"
      MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )
  endforeach()

  # Main UI app bundle
  add_executable(${CEF_UI_TARGET} MACOSX_BUNDLE ${OWL_UI_SRCS} ${OWL_HEADERS} ${OWL_UI_HEADERS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_UI_TARGET})
  add_dependencies(${CEF_UI_TARGET} libcef_dll_wrapper)
  target_link_libraries(${CEF_UI_TARGET} libcef_dll_wrapper ${CEF_STANDARD_LIBS} z curl ${MAXMINDDB_LIBRARY} ${SQLCIPHER_LIBRARY}
    OpenSSL::SSL OpenSSL::Crypto "-framework IOKit" "-framework Security" "-framework CoreFoundation")

  # Define BUILD_UI for UI-specific code
  target_compile_definitions(${CEF_UI_TARGET} PRIVATE BUILD_UI=1)

  # Link with Cocoa framework for macOS UI
  target_link_libraries(${CEF_UI_TARGET} "-framework Cocoa")
  target_link_libraries(${CEF_UI_TARGET} "-framework QuartzCore")
  target_link_libraries(${CEF_UI_TARGET} "-framework UniformTypeIdentifiers")

  # Set rpath so the executable can find CEF framework at runtime
  set_target_properties(${CEF_UI_TARGET} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks"
  )

  # Make UI app depend on all helpers
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 1 _target_suffix)
    set(_ui_helper_target "${CEF_UI_HELPER_TARGET}${_target_suffix}")
    add_dependencies(${CEF_UI_TARGET} "${_ui_helper_target}")
  endforeach()

  # Copy CEF framework to UI app
  COPY_MAC_FRAMEWORK("${CEF_UI_TARGET}" "${CEF_BINARY_DIR}" "${CEF_UI_APP}")

  # Copy helpers and CEF framework to each UI helper
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    set(_ui_helper_target "${CEF_UI_HELPER_TARGET}${_target_suffix}")
    set(_ui_helper_output_name "${CEF_UI_HELPER_OUTPUT_NAME}${_name_suffix}")

    # Copy helper to UI app's Frameworks
    add_custom_command(
      TARGET ${CEF_UI_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CEF_TARGET_OUT_DIR}/${_ui_helper_output_name}.app"
              "${CEF_UI_APP}/Contents/Frameworks/${_ui_helper_output_name}.app"
      VERBATIM
    )

    # Create symlink to CEF framework in helper
    add_custom_command(
      TARGET ${CEF_UI_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${CEF_UI_APP}/Contents/Frameworks/${_ui_helper_output_name}.app/Contents/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "../../../Chromium Embedded Framework.framework"
              "${CEF_UI_APP}/Contents/Frameworks/${_ui_helper_output_name}.app/Contents/Frameworks/Chromium Embedded Framework.framework"
      VERBATIM
    )
  endforeach()

  # UI version specific settings
  set_target_properties(${CEF_UI_TARGET} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  )

  # Copy custom Info.plist for UI version (SHOWS in Dock)
  set(UI_INFO_PLIST_FILE "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Info.plist")
  add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info-UI.plist.template"
      "${UI_INFO_PLIST_FILE}"
    COMMENT "Copying Info.plist for UI version (visible in Dock)"
  )

  # Copy app icon and logo for UI version
  set(UI_ICON_FILE "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/icon.icns")
  add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns"
      "${UI_ICON_FILE}"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/Olib-owl.apng"
      "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/Olib-owl.apng"
    COMMENT "Copying app icon and logo to UI bundle"
  )

  # Copy llama-server and dependencies to UI app (only if BUILD_WITH_LLAMA is ON)
  if(BUILD_WITH_LLAMA)
    if(EXISTS "${LLAMA_SERVER_BIN}")
      # Define BUILD_WITH_LLAMA=1 so code knows LLM is available
      target_compile_definitions(${CEF_UI_TARGET} PRIVATE BUILD_WITH_LLAMA=1)
      # Also define for UI helper targets that compile owl_browser_manager.cc
      foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
        string(REPLACE ":" ";" _suffix_list ${_suffix_list})
        list(GET _suffix_list 1 _target_suffix)
        set(_ui_helper_target "${CEF_UI_HELPER_TARGET}${_target_suffix}")
        target_compile_definitions(${_ui_helper_target} PRIVATE BUILD_WITH_LLAMA=1)
      endforeach()
      add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${LLAMA_SERVER_BIN}"
          "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/MacOS/llama-server"
        COMMAND chmod +x
          "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/MacOS/llama-server"
        COMMENT "Copying llama-server binary to UI app bundle"
      )

      file(GLOB LLAMA_DYLIBS "${LLAMA_BIN_DIR}/*.dylib")
      if(LLAMA_DYLIBS)
        foreach(DYLIB ${LLAMA_DYLIBS})
          add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
              "${DYLIB}"
              "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/MacOS/"
            COMMENT "Copying llama dylib to UI app: ${DYLIB}"
          )
        endforeach()
      endif()

      set(METAL_SHADER "${LLAMA_BIN_DIR}/ggml-metal.metal")
      if(EXISTS "${METAL_SHADER}")
        add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy
            "${METAL_SHADER}"
            "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/MacOS/ggml-metal.metal"
          COMMENT "Copying Metal shader to UI app"
        )
      endif()
    endif()

    # Copy models to UI app
    if(EXISTS "${MODEL_FILE}")
      add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/models"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${MODEL_FILE}"
          "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/models/llm-assist.gguf"
        COMMENT "Copying LLM model to UI app bundle"
      )
    endif()

    if(EXISTS "${MMPROJ_FILE}")
      add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${MMPROJ_FILE}"
          "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/models/mmproj-llm-assist.gguf"
        COMMENT "Copying mmproj file to UI app bundle"
      )
    endif()
  else()
    message(STATUS "Skipping llama-server and models for UI build (BUILD_WITH_LLAMA=OFF)")
  endif()

  # Copy GeoLite2 database to UI app bundle
  if(EXISTS "${GEOLITE2_FILE}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/third_party"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${GEOLITE2_FILE}"
        "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/third_party/GeoLite2-City.mmdb"
      COMMENT "Copying GeoLite2 database to UI app bundle"
    )
  endif()

  # Copy statics folder to UI app (includes assets/gestures for virtual camera)
  if(EXISTS "${STATICS_DIR}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${STATICS_DIR}"
        "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/statics"
      COMMENT "Copying statics folder to UI app bundle"
    )
  endif()

  # Copy VM profiles database to UI app bundle
  if(EXISTS "${VM_PROFILES_DB}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/data/profiles"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${VM_PROFILES_DB}"
        "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app/Contents/Resources/data/profiles/vm_profiles.db"
      COMMENT "Copying VM profiles database to UI app bundle"
    )
  endif()

  # Cleanup: Remove ALL helper apps from Release folder (they are inside the .app bundles)
  # This includes both UI helpers and any leftover headless helpers
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    # Clean UI helpers
    set(_ui_helper_output_name "${CEF_UI_HELPER_OUTPUT_NAME}${_name_suffix}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CEF_TARGET_OUT_DIR}/${_ui_helper_output_name}.app"
      COMMENT "Cleaning up ${_ui_helper_output_name}.app from Release folder"
      VERBATIM
    )
    # Clean headless helpers (in case they exist from previous builds)
    set(_headless_helper_output_name "owl_browser Helper${_name_suffix}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CEF_TARGET_OUT_DIR}/${_headless_helper_output_name}.app"
      COMMENT "Cleaning up ${_headless_helper_output_name}.app from Release folder"
      VERBATIM
    )
  endforeach()

  # Rename app bundle from owl_browser_ui.app to "Owl Browser.app"
  add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      "${CEF_TARGET_OUT_DIR}/Owl Browser.app"
    COMMAND ${CMAKE_COMMAND} -E rename
      "${CEF_TARGET_OUT_DIR}/${CEF_UI_TARGET}.app"
      "${CEF_TARGET_OUT_DIR}/Owl Browser.app"
    COMMENT "Renaming app bundle to 'Owl Browser.app'"
    VERBATIM
  )

  message(STATUS "UI version will be built at: Owl Browser.app (visible in Dock)")

endif()

if(OS_LINUX)
  # UI version for Linux with GTK3 and librsvg for SVG rendering
  set(CEF_UI_TARGET "owl_browser_ui")

  # Find GTK3 and librsvg using pkg-config
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
  pkg_check_modules(LIBRSVG REQUIRED librsvg-2.0)

  add_executable(${CEF_UI_TARGET} ${OWL_UI_SRCS} ${OWL_HEADERS} ${OWL_UI_HEADERS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_UI_TARGET})
  add_dependencies(${CEF_UI_TARGET} libcef_dll_wrapper)

  # Add include directories for GTK3 and librsvg
  target_include_directories(${CEF_UI_TARGET} PRIVATE
    ${GTK3_INCLUDE_DIRS}
    ${LIBRSVG_INCLUDE_DIRS}
  )

  # Add compile flags for GTK3 and librsvg
  target_compile_options(${CEF_UI_TARGET} PRIVATE ${GTK3_CFLAGS_OTHER} ${LIBRSVG_CFLAGS_OTHER})

  target_link_libraries(${CEF_UI_TARGET}
    libcef_dll_wrapper
    ${CEF_ROOT}/Release/libcef.so
    ${CEF_STANDARD_LIBS}
    z
    curl
    ${MAXMINDDB_LIBRARY}
    ${SQLCIPHER_LIBRARY}
    crypto
    ssl
    ${GTK3_LIBRARIES}
    ${LIBRSVG_LIBRARIES}
  )
  target_compile_definitions(${CEF_UI_TARGET} PRIVATE BUILD_UI=1 OWL_HAS_GTK=1)

  # Add BUILD_WITH_LLAMA compile definition if option is ON and llama-server exists
  if(BUILD_WITH_LLAMA AND EXISTS "${LLAMA_SERVER_BIN}")
    target_compile_definitions(${CEF_UI_TARGET} PRIVATE BUILD_WITH_LLAMA=1)
  endif()

  # CRITICAL: Set RPATH to use $ORIGIN so executable finds libraries relative to itself
  set_target_properties(${CEF_UI_TARGET} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
  )

  # Copy CEF files
  add_custom_command(
    TARGET ${CEF_UI_TARGET}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Release"
            "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources"
            "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>"
    VERBATIM
  )

  # Copy app resources (logo, icon, statics) for Linux UI
  set(UI_LOGO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/Olib-owl.apng")
  if(EXISTS "${UI_LOGO_FILE}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
        "${UI_LOGO_FILE}"
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/Olib-owl.apng"
      COMMENT "Copying Olib-owl.apng for UI"
    )
  endif()

  set(UI_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png")
  if(EXISTS "${UI_ICON_FILE}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
        "${UI_ICON_FILE}"
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/icon.png"
      COMMENT "Copying icon.png for UI"
    )
  endif()

  set(UI_STATICS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/statics")
  if(EXISTS "${UI_STATICS_DIR}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${UI_STATICS_DIR}"
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/statics"
      COMMENT "Copying statics folder for UI"
    )
  endif()

  # Copy VM profiles database for Linux UI
  if(EXISTS "${VM_PROFILES_DB}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/data/profiles"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${VM_PROFILES_DB}"
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/data/profiles/vm_profiles.db"
      COMMENT "Copying VM profiles database for UI"
    )
  endif()

  # Copy llama-server and dependencies for Linux UI (only if BUILD_WITH_LLAMA is ON)
  if(BUILD_WITH_LLAMA)
    if(EXISTS "${LLAMA_SERVER_BIN}")
      add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
          "${LLAMA_SERVER_BIN}"
          "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/llama-server"
        COMMAND chmod +x
          "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/llama-server"
        COMMENT "Copying llama-server binary for UI"
      )

      # Copy shared libraries
      file(GLOB LLAMA_UI_LIBS "${LLAMA_BIN_DIR}/*.so*")
      if(LLAMA_UI_LIBS)
        foreach(LIB ${LLAMA_UI_LIBS})
          add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
              "${LIB}"
              "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/"
            COMMENT "Copying llama library for UI: ${LIB}"
          )
        endforeach()
      endif()
    endif()

    # Copy models for UI
    set(UI_MODEL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/llm-assist.gguf")
    if(EXISTS "${UI_MODEL_FILE}")
      add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/models"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${UI_MODEL_FILE}"
          "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/models/llm-assist.gguf"
        COMMENT "Copying LLM model for UI"
      )
    endif()

    # Copy mmproj for UI
    set(UI_MMPROJ_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/mmproj-llm-assist.gguf")
    if(EXISTS "${UI_MMPROJ_FILE}")
      add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/models"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${UI_MMPROJ_FILE}"
          "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/models/mmproj-llm-assist.gguf"
        COMMENT "Copying mmproj file for UI"
      )
    endif()
  else()
    message(STATUS "Skipping llama-server and models for Linux UI build (BUILD_WITH_LLAMA=OFF)")
  endif()

  # Copy GeoLite2 database for UI
  set(UI_GEOLITE2_FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GeoLite2-City.mmdb")
  if(EXISTS "${UI_GEOLITE2_FILE}")
    add_custom_command(TARGET ${CEF_UI_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/third_party"
      COMMAND ${CMAKE_COMMAND} -E copy
        "${UI_GEOLITE2_FILE}"
        "$<TARGET_FILE_DIR:${CEF_UI_TARGET}>/third_party/GeoLite2-City.mmdb"
      COMMENT "Copying GeoLite2 database for UI"
    )
  endif()

  message(STATUS "Linux UI version will be built with GTK3 and librsvg")
  message(STATUS "  GTK3: ${GTK3_VERSION}")
  message(STATUS "  librsvg: ${LIBRSVG_VERSION}")
endif()

# ============================================================================
# LICENSE GENERATOR TOOL
# ============================================================================

set(LICENSE_GENERATOR_TARGET "license_generator")
add_executable(${LICENSE_GENERATOR_TARGET} scripts/license_generator.cpp)

# Platform-specific libraries for license generator
if(OS_MACOS)
  target_link_libraries(${LICENSE_GENERATOR_TARGET}
    "-framework Security"
    "-framework CoreFoundation"
    "-framework IOKit"
  )
elseif(OS_LINUX)
  target_link_libraries(${LICENSE_GENERATOR_TARGET}
    crypto
    ssl
  )
endif()

set_target_properties(${LICENSE_GENERATOR_TARGET} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

message(STATUS "License generator will be built at: ${LICENSE_GENERATOR_TARGET}")

# ============================================================================
# HTTP SERVER FOR REST API
# ============================================================================

set(HTTP_SERVER_TARGET "owl_http_server")

# HTTP Server source files
set(HTTP_SERVER_SRCS
  http-server/src/main.c
  http-server/src/config.c
  http-server/src/config_file.c
  http-server/src/log.c
  http-server/src/json.c
  http-server/src/auth.c
  http-server/src/jwt.c
  http-server/src/browser_ipc.c
  http-server/src/browser_ipc_async.c
  http-server/src/thread_pool.c
  http-server/src/tools.c
  http-server/src/router.c
  http-server/src/http_server.c
  http-server/src/websocket.c
  http-server/src/rate_limit.c
  http-server/src/ip_filter.c
  http-server/src/video_stream.c
  http-server/src/license_manager.c
  http-server/src/playground.c
  http-server/src/shm_frame_reader.c
  http-server/src/ipc_tests.c
  src/media/shared_frame_buffer.c
)

add_executable(${HTTP_SERVER_TARGET} ${HTTP_SERVER_SRCS})

# Include directories for HTTP server
target_include_directories(${HTTP_SERVER_TARGET} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/http-server/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include  # For shared_frame_buffer.h
)

# C standard
set_target_properties(${HTTP_SERVER_TARGET} PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
)

# Find OpenSSL for JWT support
find_package(OpenSSL REQUIRED)

# Platform-specific libraries for HTTP server
if(OS_MACOS)
  # Find uuid on macOS (part of system)
  target_link_libraries(${HTTP_SERVER_TARGET}
    pthread
    OpenSSL::SSL
    OpenSSL::Crypto
  )
  # macOS includes uuid in system libraries
elseif(OS_LINUX)
  # Find uuid library on Linux
  find_library(UUID_LIBRARY uuid)
  if(NOT UUID_LIBRARY)
    message(WARNING "libuuid not found. Install with: sudo apt-get install uuid-dev")
  endif()
  target_link_libraries(${HTTP_SERVER_TARGET}
    pthread
    ${UUID_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
  )
endif()

# RPATH settings for finding libraries
if(OS_LINUX)
  set_target_properties(${HTTP_SERVER_TARGET} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
  )
endif()

message(STATUS "HTTP Server will be built at: ${HTTP_SERVER_TARGET}")

# Copy CEF files
if(EXISTS "${CEF_ROOT}/Release")
  file(GLOB CEF_BINARY_FILES
    "${CEF_ROOT}/Release/*.dylib"
    "${CEF_ROOT}/Release/*.so"
    "${CEF_ROOT}/Release/*.dll"
  )
  file(GLOB CEF_RESOURCE_FILES
    "${CEF_ROOT}/Resources/*"
  )
endif()
