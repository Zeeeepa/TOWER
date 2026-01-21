#include "owl_homepage.h"
#include "owl_browser_manager.h"
#include "owl_demographics.h"
#include "owl_llm_client.h"
#include "logger.h"
#include "../resources/icons/icons.h"
#include <sstream>
#include <iomanip>
#include <ctime>

std::string OwlHomepage::GenerateHomepage(OwlBrowserManager* manager) {
  std::ostringstream html;

  html << GenerateHeader();
  html << R"HTML(
<body>
  <div class="homepage-container">
    <!-- Logo - APNG plays once and stops on last frame -->
    <div class="logo">
      <img id="owl-logo" src="owl://resources/Olib-owl.apng" alt="Owl Browser" style="width: 180px; height: auto;" />
    </div>

    <!-- Browser Info Bar -->
)HTML";

  html << GenerateBrowserInfoBar(manager);

  html << R"HTML(
    <!-- AI Command Box -->
    <div class="ai-command-container">
      <div class="ai-command-box">
        <span class="command-icon">)HTML" << OlibIcons::MAGIC_WAND_SPARKLES << R"HTML(</span>
        <input type="text" id="ai-command-input"
               placeholder="Tell me what to do... (e.g., 'go to google.com and search for banana')"
               autocomplete="off">
        <button id="execute-command-btn" class="go-button" title="Execute Command">
          Go
        </button>
      </div>
      <div class="command-hint">Press Enter or click Go • AI will understand and automate your command</div>

      <!-- Task Progress Overlay -->
      <div id="task-overlay" class="task-overlay" style="display: none;">
        <div class="task-header">
          <span id="task-status">Planning...</span>
          <button id="task-close-btn" class="task-close-btn" onclick="hideTaskOverlay()">×</button>
        </div>

        <!-- Thinking/Planning Stage -->
        <div id="thinking-stage" class="thinking-stage" style="display: none;">
          <div class="thinking-indicator">
            <div class="thinking-dots">
              <span></span><span></span><span></span>
            </div>
            <span id="thinking-text">LLM is analyzing your command...</span>
          </div>
          <div id="reasoning-box" class="reasoning-box" style="display: none;">
            <div class="reasoning-label">
              <svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 4px;" viewBox="0 0 672 672">)HTML" << OlibIcons::COMMENTS << R"HTML(</svg>
              Reasoning:
            </div>
            <div id="reasoning-content" class="reasoning-content"></div>
          </div>
        </div>

        <div class="task-progress-bar">
          <div id="task-progress-fill" class="task-progress-fill"></div>
        </div>
        <div id="task-list" class="task-list"></div>
      </div>
    </div>

    <!-- Action Cards -->
    <div class="action-cards">
      <div class="action-card" onclick="window.location.href='#stats'">
        <div class="card-icon">)HTML" << OlibIcons::CHART_SIMPLE << R"HTML(</div>
        <div class="card-title">Browser Stats</div>
        <div class="card-desc">View analytics and metrics</div>
      </div>
      <div class="action-card" onclick="window.location.href='#settings'">
        <div class="card-icon">)HTML" << OlibIcons::GEAR << R"HTML(</div>
        <div class="card-title">LLM Settings</div>
        <div class="card-desc">Configure AI providers</div>
      </div>
      <div class="action-card" onclick="openPlaygroundWindow()">
        <div class="card-icon">)HTML" << OlibIcons::GAMEPAD << R"HTML(</div>
        <div class="card-title">Developer Playground</div>
        <div class="card-desc">Build and test automation flows</div>
      </div>
    </div>
  </div>

  <!-- Stats View (initially hidden) -->
  <div id="stats" class="view">
)HTML";

  html << GenerateLLMStatusCard(manager);
  html << GenerateBrowserInfoCard();

  html << R"HTML(
  </div>

  <!-- Settings View (initially hidden) -->
  <div id="settings" class="view">
    <div class="settings-container">
      <h2 class="section-title"><svg style="width: 1.2em; height: 1.2em; display: inline-block; vertical-align: middle; margin-right: 8px;" viewBox="0 0 672 672">)HTML" << OlibIcons::GEAR << R"HTML(</svg>LLM Settings</h2>
      <p class="section-subtitle">Manage your Language Model providers</p>

      <!-- Active Provider Display -->
      <div id="activeProviderCard" class="active-provider-card)HTML";
#if !BUILD_WITH_LLAMA
  html << R"HTML( no-provider)HTML";
#endif
  html << R"HTML(">
        <div class="active-provider-header">
          <span id="activeBadge" class="active-badge)HTML";
#if !BUILD_WITH_LLAMA
  html << R"HTML( hidden)HTML";
#endif
  html << R"HTML(">ACTIVE</span>
          <span id="activeProviderName" class="provider-name">)HTML";
#if BUILD_WITH_LLAMA
  html << R"HTML(Built-in Model (Qwen3-VL-2B))HTML";
#else
  html << R"HTML(No Provider Configured)HTML";
#endif
  html << R"HTML(</span>
        </div>
        <div id="activeProviderDetails" class="provider-details">
          )HTML";
#if BUILD_WITH_LLAMA
  html << R"HTML(Local on-device model - Fast & Private)HTML";
#else
  html << R"HTML(Please add an external LLM provider below)HTML";
#endif
  html << R"HTML(
        </div>
        <!-- LLM Control Buttons -->
        <div class="llm-control-buttons">)HTML";
#if BUILD_WITH_LLAMA
  html << R"HTML(
          <button id="btnUseBuiltin" onclick="useBuiltinLLM()" class="btn-llm-control btn-builtin">
            <svg style="width: 1em; height: 1em; margin-right: 6px;" viewBox="0 0 512 512"><path fill="currentColor" d="M464 256A208 208 0 1 0 48 256a208 208 0 1 0 416 0zM0 256a256 256 0 1 1 512 0A256 256 0 1 1 0 256zm256-96a96 96 0 1 1 0 192 96 96 0 1 1 0-192z"/></svg>
            Use Built-in
          </button>)HTML";
#endif
  html << R"HTML(
          <button id="btnDisableLLM" onclick="disableLLM()" class="btn-llm-control btn-disable">
            <svg style="width: 1em; height: 1em; margin-right: 6px;" viewBox="0 0 512 512"><path fill="currentColor" d="M367.2 412.5L99.5 144.8C77.1 176.1 64 214.5 64 256c0 106 86 192 192 192c41.5 0 79.9-13.1 111.2-35.5zm45.3-45.3C434.9 335.9 448 297.5 448 256c0-106-86-192-192-192c-41.5 0-79.9 13.1-111.2 35.5L412.5 367.2zM0 256a256 256 0 1 1 512 0A256 256 0 1 1 0 256z"/></svg>
            Disable LLM
          </button>
        </div>
      </div>

      <!-- Add Provider Button -->
      <div class="add-provider-section">
        <button onclick="showAddProviderForm()" class="btn-add-provider">
          <svg style="width: 1em; height: 1em; margin-right: 8px;" viewBox="0 0 448 512"><path fill="currentColor" d="M432 256c0 17.69-14.33 32.01-32 32.01H256v144c0 17.69-14.33 31.99-32 31.99s-32-14.3-32-31.99v-144H48c-17.67 0-32-14.32-32-32.01s14.33-31.99 32-31.99H192v-144c0-17.69 14.33-32.01 32-32.01s32 14.32 32 32.01v144h144C417.7 224 432 238.3 432 256z"/></svg>
          Add LLM Provider
        </button>
      </div>

      <!-- Add/Edit Provider Form (initially hidden) -->
      <div id="providerForm" class="provider-form" style="display: none;">
        <h3 id="formTitle" class="form-title">Add LLM Provider</h3>
        <form id="llmProviderForm" onsubmit="saveProvider(event)">
          <input type="hidden" id="providerId" value="">

          <div class="form-group">
            <label for="providerName">Provider Name *</label>
            <input type="text" id="providerNameInput" required placeholder="e.g., OpenAI GPT-4, Azure, Ollama">
          </div>

          <div class="form-group">
            <label for="providerEndpoint">API Endpoint *</label>
            <input type="text" id="providerEndpoint" required placeholder="https://api.openai.com">
            <small>Full URL to the OpenAI-compatible API endpoint</small>
          </div>

          <div class="form-group">
            <label for="providerModel">Model Name *</label>
            <input type="text" id="providerModel" required placeholder="gpt-4-vision-preview">
            <small>Model identifier (e.g., gpt-4o, gpt-4-vision-preview)</small>
          </div>

          <div class="form-group">
            <label for="providerApiKey">API Key</label>
            <input type="password" id="providerApiKey" placeholder="sk-...">
            <small>Leave empty if no authentication required</small>
          </div>

          <div class="form-group">
            <label class="checkbox-label">
              <input type="checkbox" id="providerIsThirdParty">
              <span>Third-party LLM (enables PII/HIPAA scrubbing)</span>
            </label>
            <small>Check this if sending data to external cloud services like OpenAI, Anthropic, etc. Leave unchecked for local/private LLMs like Ollama, LMStudio on your network.</small>
          </div>

          <div class="form-group">
            <label class="checkbox-label">
              <input type="checkbox" id="providerActive">
              <span>Set as active provider</span>
            </label>
          </div>

          <!-- Test Connection Button -->
          <div class="test-connection-section">
            <button type="button" onclick="testLLMConnection()" class="btn-test">
              <svg style="width: 1em; height: 1em; margin-right: 6px;" viewBox="0 0 512 512"><path fill="currentColor" d="M256 8C119.033 8 8 119.033 8 256s111.033 248 248 248 248-111.033 248-248S392.967 8 256 8zm0 48c110.532 0 200 89.451 200 200 0 110.532-89.451 200-200 200-110.532 0-200-89.451-200-200 0-110.532 89.451-200 200-200m140.204 130.267l-22.536-22.718c-4.667-4.705-12.265-4.736-16.97-.068L215.346 303.697l-59.792-60.277c-4.667-4.705-12.265-4.736-16.970-.069l-22.719 22.536c-4.705 4.667-4.736 12.265-.068 16.971l90.781 91.516c4.667 4.705 12.265 4.736 16.97.068l172.589-171.204c4.704-4.668 4.734-12.266.067-16.971z"/></svg>
              Test Connection
            </button>
            <div id="testResult" class="test-result"></div>
          </div>

          <div class="form-actions">
            <button type="submit" class="btn-primary">Save Provider</button>
            <button type="button" onclick="cancelProviderForm()" class="btn-secondary">Cancel</button>
          </div>
        </form>
      </div>

      <!-- Providers List -->
      <div class="providers-list">
        <h3 class="list-title">Configured Providers</h3>
        <div id="providersList">
          <!-- Providers will be dynamically inserted here -->
        </div>
      </div>
    </div>
  </div>
)HTML";

  html << GenerateFooter(manager);

  return html.str();
}

std::string OwlHomepage::GenerateHeader() {
  return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Owl Browser</title>
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }

    body {
      font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, 'Roboto', sans-serif;
      background: #ffffff;
      color: #202124;
      overflow-x: hidden;
      min-height: 100vh;
    }

    /* Homepage Container */
    .homepage-container {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      padding: 40px 20px;
    }

    /* Logo */
    .logo {
      margin-bottom: 20px;
    }

    .logo img {
      width: 180px;
      height: auto;
    }

    /* Info Bar */
    .info-bar {
      display: flex;
      gap: 24px;
      margin-bottom: 32px;
      padding: 16px 32px;
      background: rgba(255, 255, 255, 0.95);
      backdrop-filter: blur(10px);
      border-radius: 16px;
      box-shadow: 0 4px 20px rgba(0, 0, 0, 0.08);
    }

    .info-item {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 14px;
      color: #5f6368;
    }

    .info-icon {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 16px;
      height: 16px;
      margin-right: 6px;
      color: #5f6368;
    }

    .info-icon svg {
      width: 100%;
      height: 100%;
      fill: #5f6368;
    }

    .info-icon svg path {
      fill: #5f6368;
    }

    /* AI Command Container */
    .ai-command-container {
      width: 100%;
      max-width: 700px;
      margin-bottom: 32px;
    }

    .ai-command-box {
      position: relative;
      width: 100%;
      background: white;
      border-radius: 28px;
      display: flex;
      align-items: center;
      padding: 0 24px;
      box-shadow: 0 4px 24px rgba(66, 133, 244, 0.2);
      transition: all 0.3s ease;
      border: 2px solid transparent;
    }

    .ai-command-box:hover {
      box-shadow: 0 8px 32px rgba(66, 133, 244, 0.3);
      border-color: #4285f4;
    }

    .ai-command-box:focus-within {
      box-shadow: 0 8px 40px rgba(66, 133, 244, 0.4);
      border-color: #4285f4;
    }

    .command-icon {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 24px;
      height: 24px;
      margin-right: 12px;
      flex-shrink: 0;
      color: #4285f4;
      animation: pulse 2s ease-in-out infinite;
    }

    .command-icon svg {
      width: 100%;
      height: 100%;
      fill: #4285f4;
    }

    .command-icon svg path {
      fill: #4285f4;
    }

    @keyframes pulse {
      0%, 100% { opacity: 0.7; transform: scale(1); }
      50% { opacity: 1; transform: scale(1.05); }
    }

    #ai-command-input {
      flex: 1;
      border: none;
      outline: none;
      font-size: 16px;
      color: #202124;
      background: transparent;
      padding: 18px 0;
    }

    #ai-command-input::placeholder {
      color: #9aa0a6;
    }

    .go-button {
      background: #4285f4;
      color: white;
      border: none;
      border-radius: 20px;
      padding: 10px 24px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s ease;
      flex-shrink: 0;
      margin-left: 12px;
    }

    .go-button:hover {
      background: #1a73e8;
      transform: scale(1.05);
    }

    .go-button:active {
      transform: scale(0.98);
    }

    .command-hint {
      text-align: center;
      font-size: 13px;
      color: #5f6368;
      margin-top: 12px;
    }

    /* Task Overlay */
    .task-overlay {
      background: white;
      border-radius: 16px;
      padding: 20px;
      margin-top: 20px;
      box-shadow: 0 4px 20px rgba(0, 0, 0, 0.12);
      border: 2px solid #4285f4;
    }

    .task-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
    }

    #task-status {
      font-size: 16px;
      font-weight: 600;
      color: #202124;
    }

    .task-close-btn {
      background: none;
      border: none;
      font-size: 24px;
      color: #5f6368;
      cursor: pointer;
      padding: 0;
      width: 28px;
      height: 28px;
      display: flex;
      align-items: center;
      justify-content: center;
      border-radius: 50%;
      transition: all 0.2s ease;
    }

    .task-close-btn:hover {
      background: #f1f3f4;
      color: #202124;
    }

    .task-progress-bar {
      width: 100%;
      height: 6px;
      background: #e0e0e0;
      border-radius: 3px;
      overflow: hidden;
      margin-bottom: 16px;
    }

    .task-progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #4285f4 0%, #34a853 100%);
      width: 0%;
      transition: width 0.3s ease;
    }

    .task-list {
      display: flex;
      flex-direction: column;
      gap: 8px;
    }

    .task-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px;
      background: #f8f9fa;
      border-radius: 8px;
      font-size: 14px;
    }

    .task-item.completed {
      opacity: 0.7;
    }

    .task-item.active {
      background: #e8f0fe;
      border-left: 3px solid #4285f4;
    }

    .task-item.failed {
      background: #fce8e6;
      border-left: 3px solid #ea4335;
    }

    .task-icon {
      width: 20px;
      height: 20px;
      display: flex;
      align-items: center;
      justify-content: center;
      flex-shrink: 0;
    }

    /* Thinking/Planning Stage */
    .thinking-stage {
      margin-bottom: 16px;
      padding: 16px;
      background: linear-gradient(135deg, #f0f7ff 0%, #e8f0fe 100%);
      border-radius: 12px;
      border: 1px solid #d2e3fc;
    }

    .thinking-indicator {
      display: flex;
      align-items: center;
      gap: 12px;
      margin-bottom: 12px;
    }

    .thinking-dots {
      display: flex;
      gap: 6px;
    }

    .thinking-dots span {
      width: 8px;
      height: 8px;
      background: #4285f4;
      border-radius: 50%;
      animation: thinking 1.4s infinite ease-in-out both;
    }

    .thinking-dots span:nth-child(1) {
      animation-delay: -0.32s;
    }

    .thinking-dots span:nth-child(2) {
      animation-delay: -0.16s;
    }

    @keyframes thinking {
      0%, 80%, 100% {
        transform: scale(0.8);
        opacity: 0.5;
      }
      40% {
        transform: scale(1.2);
        opacity: 1;
      }
    }

    #thinking-text {
      font-size: 14px;
      color: #1a73e8;
      font-weight: 500;
    }

    .reasoning-box {
      background: white;
      border-radius: 8px;
      padding: 12px;
      margin-top: 12px;
      border-left: 3px solid #fbbc04;
    }

    .reasoning-label {
      font-size: 13px;
      font-weight: 600;
      color: #ea8600;
      margin-bottom: 8px;
    }

    .reasoning-content {
      font-size: 13px;
      line-height: 1.5;
      color: #5f6368;
      white-space: pre-wrap;
    }

    /* Action Cards */
    .action-cards {
      display: flex;
      gap: 20px;
      flex-wrap: wrap;
      justify-content: center;
    }

    .action-card {
      background: white;
      border-radius: 20px;
      padding: 28px 32px;
      min-width: 220px;
      cursor: pointer;
      transition: all 0.3s ease;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.08);
      text-align: center;
    }

    .action-card:hover {
      transform: translateY(-8px);
      box-shadow: 0 12px 32px rgba(0, 0, 0, 0.15);
    }

    .card-icon {
      display: flex;
      align-items: center;
      justify-content: center;
      width: 64px;
      height: 64px;
      margin: 0 auto 16px;
      color: #4285f4;
    }

    .card-icon svg {
      width: 100%;
      height: 100%;
      fill: #4285f4;
    }

    .card-icon svg path {
      fill: #4285f4;
    }

    .btn-icon {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 20px;
      height: 20px;
      margin-right: 8px;
      vertical-align: middle;
    }

    .btn-icon svg {
      width: 100%;
      height: 100%;
      fill: currentColor;
    }

    .btn-icon svg path {
      fill: currentColor;
    }

    .card-title {
      font-size: 18px;
      font-weight: 600;
      color: #202124;
      margin-bottom: 8px;
    }

    .card-desc {
      font-size: 13px;
      color: #5f6368;
    }

    .hero h1 {
      font-size: 48px;
      font-weight: 700;
      margin-bottom: 10px;
      color: #629f85;
    }

    .subtitle {
      font-size: 20px;
      margin-bottom: 20px;
      opacity: 0.85;
      color: #629f85;
    }

    .badge {
      display: inline-block;
      background: rgba(98, 159, 133, 0.1);
      padding: 10px 20px;
      border-radius: 20px;
      font-size: 14px;
      border: 1px solid rgba(98, 159, 133, 0.3);
      color: #629f85;
    }

    .card {
      background: white;
      border-radius: 16px;
      padding: 30px;
      margin-bottom: 20px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.1);
      animation: fadeInUp 0.8s ease-out;
      transition: transform 0.3s ease, box-shadow 0.3s ease;
    }

    .card:hover {
      transform: translateY(-5px);
      box-shadow: 0 15px 40px rgba(0,0,0,0.15);
    }

    .card-title {
      font-size: 24px;
      margin-bottom: 20px;
      color: #629f85;
      display: flex;
      align-items: center;
      gap: 10px;
    }

    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 20px;
      margin-top: 20px;
    }

    .stat-item {
      background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
      padding: 20px;
      border-radius: 12px;
      text-align: center;
      transition: transform 0.2s ease;
    }

    .stat-item:hover {
      transform: scale(1.05);
    }

    .stat-icon {
      font-size: 32px;
      margin-bottom: 10px;
      display: flex;
      align-items: center;
      justify-content: center;
      height: 40px;
    }

    .stat-icon svg {
      width: 32px;
      height: 32px;
      fill: currentColor;
      color: #666;
    }

    .stat-label {
      font-size: 12px;
      color: #666;
      text-transform: uppercase;
      letter-spacing: 1px;
      margin-bottom: 5px;
    }

    .stat-value {
      font-size: 24px;
      font-weight: 700;
      color: #333;
    }

    .status-indicator {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 8px 16px;
      border-radius: 20px;
      font-size: 14px;
      font-weight: 600;
      margin-top: 10px;
    }

    .status-online {
      background: #d4edda;
      color: #155724;
    }

    .status-offline {
      background: #f8d7da;
      color: #721c24;
    }

    .status-loading {
      background: #fff3cd;
      color: #856404;
    }

    .status-info {
      background: #d1ecf1;
      color: #0c5460;
    }

    .status-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      animation: pulse 2s infinite;
    }

    .status-online .status-dot {
      background: #28a745;
    }

    .status-offline .status-dot {
      background: #dc3545;
    }

    .status-loading .status-dot {
      background: #ffc107;
    }

    .status-info .status-dot {
      background: #17a2b8;
    }

    .info-row {
      display: flex;
      justify-content: space-between;
      padding: 12px 0;
      border-bottom: 1px solid #f0f0f0;
    }

    .info-row:last-child {
      border-bottom: none;
    }

    .info-label {
      color: #666;
      font-weight: 600;
    }

    .info-value {
      color: #333;
      font-family: 'Monaco', 'Courier New', monospace;
    }

    .quick-actions {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
      gap: 20px;
      margin-top: 20px;
    }

    .action-item {
      text-align: center;
      padding: 30px 20px;
      background: #629f85;
      color: white;
      border-radius: 12px;
      transition: transform 0.3s ease;
      cursor: pointer;
    }

    .action-item:hover {
      transform: scale(1.05);
    }

    .action-icon {
      font-size: 48px;
      margin-bottom: 10px;
    }

    .action-item h3 {
      font-size: 18px;
      margin-bottom: 8px;
    }

    .action-item p {
      font-size: 14px;
      opacity: 0.9;
    }

    .footer {
      text-align: center;
      color: #629f85;
      margin-top: 40px;
      padding: 20px;
    }

    .footer-links {
      margin-top: 10px;
    }

    .footer-links a {
      color: #629f85;
      text-decoration: none;
      margin: 0 10px;
      opacity: 0.8;
      transition: opacity 0.3s ease;
    }

    .footer-links a:hover {
      opacity: 1;
      text-decoration: underline;
    }

    @keyframes fadeInDown {
      from {
        opacity: 0;
        transform: translateY(-20px);
      }
      to {
        opacity: 1;
        transform: translateY(0);
      }
    }

    @keyframes fadeInUp {
      from {
        opacity: 0;
        transform: translateY(20px);
      }
      to {
        opacity: 1;
        transform: translateY(0);
      }
    }

    @keyframes bounce {
      0%, 100% {
        transform: translateY(0);
      }
      50% {
        transform: translateY(-10px);
      }
    }

    @keyframes pulse {
      0%, 100% {
        opacity: 1;
      }
      50% {
        opacity: 0.5;
      }
    }

    @keyframes spin {
      from {
        transform: rotate(0deg);
      }
      to {
        transform: rotate(360deg);
      }
    }

    .weather-info {
      display: flex;
      align-items: center;
      gap: 20px;
      padding: 20px;
      background: linear-gradient(135deg, #84fab0 0%, #8fd3f4 100%);
      border-radius: 12px;
      color: white;
      margin-top: 20px;
    }

    .weather-icon {
      font-size: 64px;
    }

    .weather-details {
      flex: 1;
    }

    .weather-temp {
      font-size: 36px;
      font-weight: 700;
      margin-bottom: 5px;
    }

    .weather-condition {
      font-size: 18px;
      opacity: 0.9;
    }

    .datetime-display {
      text-align: center;
      padding: 20px;
      background: linear-gradient(135deg, #fa709a 0%, #fee140 100%);
      border-radius: 12px;
      color: white;
      margin-top: 20px;
    }

    .datetime-display .time {
      font-size: 48px;
      font-weight: 700;
      margin-bottom: 10px;
      font-family: 'Monaco', 'Courier New', monospace;
    }

    .datetime-display .date {
      font-size: 20px;
      opacity: 0.95;
    }

    /* View Management */
    .view {
      display: none;
      width: 100%;
      max-width: 1200px;
      margin: 0 auto;
      padding: 20px;
    }

    .view.active {
      display: block;
    }

    /* Settings View Styles */
    .settings-container {
      max-width: 800px;
      margin: 0 auto;
    }

    .section-title {
      font-size: 32px;
      color: #202124;
      margin-bottom: 8px;
      font-weight: 600;
    }

    .section-subtitle {
      color: #5f6368;
      font-size: 16px;
      margin-bottom: 30px;
    }

    .active-provider-card {
      border-radius: 12px;
      padding: 24px;
      margin-bottom: 24px;
      background: linear-gradient(135deg, #4285f4 0%, #1a73e8 100%);
      color: white;
      box-shadow: 0 4px 12px rgba(66, 133, 244, 0.3);
    }

    .active-provider-card.no-provider {
      background: linear-gradient(135deg, #fff3cd 0%, #ffffff 100%);
      color: #856404;
      border-left: 4px solid #ffc107;
      box-shadow: 0 4px 12px rgba(255, 193, 7, 0.2);
    }

    .active-provider-header {
      display: flex;
      align-items: center;
      gap: 12px;
      margin-bottom: 8px;
    }

    .active-badge {
      background: rgba(255, 255, 255, 0.3);
      padding: 4px 12px;
      border-radius: 12px;
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 0.5px;
    }

    .active-badge.hidden {
      display: none;
    }

    .provider-name {
      font-size: 20px;
      font-weight: 600;
    }

    .provider-details {
      font-size: 14px;
      opacity: 0.9;
    }

    .llm-control-buttons {
      display: flex;
      gap: 12px;
      margin-top: 16px;
      flex-wrap: wrap;
    }

    .btn-llm-control {
      padding: 8px 16px;
      border-radius: 20px;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      border: none;
    }

    .btn-llm-control.btn-builtin {
      background: rgba(255, 255, 255, 0.2);
      color: white;
      border: 1px solid rgba(255, 255, 255, 0.4);
    }

    .btn-llm-control.btn-builtin:hover {
      background: rgba(255, 255, 255, 0.3);
    }

    .btn-llm-control.btn-builtin.active {
      background: white;
      color: #1a73e8;
    }

    .btn-llm-control.btn-disable {
      background: rgba(255, 255, 255, 0.15);
      color: rgba(255, 255, 255, 0.9);
      border: 1px solid rgba(255, 255, 255, 0.3);
    }

    .btn-llm-control.btn-disable:hover {
      background: rgba(234, 67, 53, 0.2);
      border-color: rgba(234, 67, 53, 0.5);
    }

    .btn-llm-control.btn-disable.active {
      background: #ea4335;
      color: white;
      border-color: #ea4335;
    }

    .active-provider-card.disabled {
      background: linear-gradient(135deg, #5f6368 0%, #3c4043 100%);
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
    }

    .add-provider-section {
      margin-bottom: 32px;
    }

    .btn-add-provider {
      background: white;
      color: #4285f4;
      border: 2px solid #4285f4;
      padding: 12px 24px;
      border-radius: 24px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      display: inline-flex;
      align-items: center;
    }

    .btn-add-provider:hover {
      background: #4285f4;
      color: white;
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(66, 133, 244, 0.3);
    }

    .provider-form {
      background: white;
      border: 2px solid #e8eaed;
      border-radius: 12px;
      padding: 24px;
      margin-bottom: 24px;
    }

    .form-title {
      font-size: 20px;
      color: #202124;
      margin-bottom: 20px;
      font-weight: 600;
    }

    .form-group {
      margin-bottom: 20px;
    }

    .form-group label {
      display: block;
      color: #5f6368;
      font-size: 14px;
      font-weight: 500;
      margin-bottom: 8px;
    }

    .form-group input[type="text"],
    .form-group input[type="password"] {
      width: 100%;
      padding: 12px;
      border: 2px solid #e8eaed;
      border-radius: 8px;
      font-size: 14px;
      transition: border-color 0.3s;
    }

    .form-group input:focus {
      outline: none;
      border-color: #4285f4;
    }

    .form-group small {
      display: block;
      color: #80868b;
      font-size: 12px;
      margin-top: 4px;
    }

    .checkbox-label {
      display: flex;
      align-items: center;
      gap: 8px;
      cursor: pointer;
    }

    .checkbox-label input[type="checkbox"] {
      width: 18px;
      height: 18px;
      cursor: pointer;
    }

    .form-actions {
      display: flex;
      gap: 12px;
      margin-top: 24px;
    }

    .btn-primary {
      background: #4285f4;
      color: white;
      border: none;
      padding: 12px 24px;
      border-radius: 24px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
    }

    .btn-primary:hover {
      background: #1a73e8;
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(66, 133, 244, 0.3);
    }

    .btn-secondary {
      background: white;
      color: #5f6368;
      border: 2px solid #e8eaed;
      padding: 12px 24px;
      border-radius: 24px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
    }

    .btn-secondary:hover {
      background: #f8f9fa;
      border-color: #dadce0;
    }

    .btn-test {
      background: #34a853;
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 20px;
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      display: inline-flex;
      align-items: center;
      margin-bottom: 12px;
    }

    .btn-test:hover {
      background: #2d8e47;
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(52, 168, 83, 0.3);
    }

    .btn-test:disabled {
      background: #ccc;
      cursor: not-allowed;
      transform: none;
    }

    .test-connection-section {
      margin: 16px 0;
      padding: 16px;
      background: #f8f9fa;
      border-radius: 8px;
    }

    .test-result {
      margin-top: 12px;
      padding: 12px;
      border-radius: 8px;
      font-size: 14px;
      display: none;
    }

    .test-result.success {
      display: block;
      background: #d4edda;
      color: #155724;
      border: 1px solid #c3e6cb;
    }

    .test-result.error {
      display: block;
      background: #f8d7da;
      color: #721c24;
      border: 1px solid #f5c6cb;
    }

    .test-result.loading {
      display: block;
      background: #fff3cd;
      color: #856404;
      border: 1px solid #ffeaa7;
    }

    .providers-list {
      margin-top: 32px;
    }

    .list-title {
      font-size: 18px;
      color: #202124;
      margin-bottom: 16px;
      font-weight: 600;
    }

    .provider-item {
      background: white;
      border: 2px solid #e8eaed;
      border-radius: 12px;
      padding: 20px;
      margin-bottom: 12px;
      transition: all 0.3s ease;
    }

    .provider-item:hover {
      border-color: #dadce0;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
    }

    .provider-item.active {
      border-color: #4285f4;
      background: #f8f9ff;
    }

    .provider-header {
      display: flex;
      justify-content: space-between;
      align-items: start;
      margin-bottom: 12px;
    }

    .provider-info h4 {
      font-size: 16px;
      color: #202124;
      margin-bottom: 4px;
    }

    .provider-info p {
      font-size: 13px;
      color: #5f6368;
    }

    .provider-actions {
      display: flex;
      gap: 8px;
    }

    .btn-action {
      padding: 8px 16px;
      border: none;
      border-radius: 16px;
      font-size: 13px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.2s ease;
    }

    .btn-activate {
      background: #e8f5e9;
      color: #1e8e3e;
    }

    .btn-activate:hover {
      background: #c8e6c9;
    }

    .btn-edit {
      background: #e3f2fd;
      color: #1967d2;
    }

    .btn-edit:hover {
      background: #bbdefb;
    }

    .btn-delete {
      background: #fce8e6;
      color: #d93025;
    }

    .btn-delete:hover {
      background: #f8bbd0;
    }
  </style>
</head>
)HTML";
}

std::string OwlHomepage::GenerateBrowserInfoBar(OwlBrowserManager* manager) {
  std::ostringstream html;

  // Get demographics data
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (demo && !demo->IsReady()) {
    demo->Initialize();
  }

  DemographicInfo info = demo ? demo->GetAllInfo() : DemographicInfo();

  html << R"HTML(
    <div class="info-bar">
      <div class="info-item">
        <span class="info-icon">)HTML" << OlibIcons::LOCATION_DOT << R"HTML(</span>
        <span>)HTML";

  if (demo && info.has_location && !info.location.city.empty()) {
    html << info.location.city << ", " << info.location.country_name;
  } else {
    html << "Unknown";
  }

  html << R"HTML(</span>
      </div>
      <div class="info-item">
        <span class="info-icon">)HTML" << OlibIcons::CLOCK << R"HTML(</span>
        <span id="current-time">)HTML" << info.datetime.time << R"HTML(</span>
      </div>
      <div class="info-item">
        <span class="info-icon">)HTML";

  if (info.has_weather) {
    std::string condition = info.weather.condition;
    if (condition.find("Cloud") != std::string::npos || condition.find("Overcast") != std::string::npos) {
      html << OlibIcons::CLOUD;
    } else if (condition.find("Rain") != std::string::npos ||
               condition.find("Snow") != std::string::npos ||
               condition.find("Storm") != std::string::npos ||
               condition.find("Thunder") != std::string::npos) {
      html << OlibIcons::CLOUD;
    } else {
      html << OlibIcons::SUN;
    }
  } else {
    html << OlibIcons::SUN;
  }

  html << R"HTML(</span>
        <span>)HTML";

  if (info.has_weather) {
    html << std::fixed << std::setprecision(1) << info.weather.temperature_celsius << "°C";
  } else {
    html << "Unknown";
  }

  html << R"HTML(</span>
      </div>
      <div class="info-item">
        <span class="info-icon">)HTML" << OlibIcons::GLOBE << R"HTML(</span>
        <span>)HTML";

  if (demo && info.has_location && !info.location.ip_address.empty()) {
    html << info.location.ip_address;
  } else {
    html << "Unknown";
  }

  html << R"HTML(</span>
      </div>
    </div>
)HTML";

  return html.str();
}

std::string OwlHomepage::GenerateDemographicsCard(OwlBrowserManager* manager) {
  std::ostringstream html;

  html << R"HTML(
    <!-- Demographics & Weather Card -->
    <div class="card">
      <h2 class="card-title"><svg style="width: 1.2em; height: 1.2em; display: inline-block; vertical-align: middle; margin-right: 8px;" viewBox="0 0 672 672">)HTML" << OlibIcons::GLOBE << R"HTML(</svg>Your Context</h2>
)HTML";

  // Get demographics data
  OwlDemographics* demo = OwlDemographics::GetInstance();
  if (demo) {
    // Try to initialize if not ready
    if (!demo->IsReady()) {
      demo->Initialize();
    }

    DemographicInfo info = demo->GetAllInfo();

    // Always show datetime (doesn't require demographics initialization)
    html << R"HTML(
      <div class="datetime-display">
        <div class="time" id="current-time">)HTML" << info.datetime.time << R"HTML(</div>
        <div class="date">)HTML"
         << info.datetime.day_of_week << ", " << info.datetime.date
         << " • " << info.datetime.timezone << R"HTML(</div>
      </div>
)HTML";

    // Location Section - only show if we actually have location data
    if (info.has_location && !info.location.city.empty()) {
      html << R"HTML(
      <div class="stats-grid">
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::BUILDING << R"HTML(</div>
          <div class="stat-label">City</div>
          <div class="stat-value">)HTML" << info.location.city << R"HTML(</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::MAP << R"HTML(</div>
          <div class="stat-label">Country</div>
          <div class="stat-value">)HTML" << info.location.country_name << R"HTML(</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::LOCATION_DOT << R"HTML(</div>
          <div class="stat-label">Coordinates</div>
          <div class="stat-value">)HTML"
           << std::fixed << std::setprecision(2)
           << info.location.latitude << ", " << info.location.longitude
           << R"HTML(</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::CLOCK << R"HTML(</div>
          <div class="stat-label">Timezone</div>
          <div class="stat-value">)HTML" << info.location.timezone << R"HTML(</div>
        </div>
      </div>
)HTML";
    }

    // Weather Section
    if (info.has_weather) {
      std::string weatherIcon;
      std::string condition = info.weather.condition;
      if (condition.find("Cloud") != std::string::npos || condition.find("Overcast") != std::string::npos) {
        weatherIcon = OlibIcons::CLOUD;
      } else if (condition.find("Rain") != std::string::npos) {
        weatherIcon = OlibIcons::CLOUD;
      } else if (condition.find("Snow") != std::string::npos) {
        weatherIcon = OlibIcons::SNOWFLAKE;
      } else if (condition.find("Storm") != std::string::npos || condition.find("Thunder") != std::string::npos) {
        weatherIcon = OlibIcons::CLOUD;
      } else if (condition.find("Fog") != std::string::npos || condition.find("Mist") != std::string::npos) {
        weatherIcon = OlibIcons::CLOUD;
      } else if (condition.find("Clear") != std::string::npos) {
        weatherIcon = OlibIcons::SUN;
      } else {
        weatherIcon = OlibIcons::SUN;
      }

      html << R"HTML(
      <div class="weather-info">
        <div class="weather-icon">)HTML" << weatherIcon << R"HTML(</div>
        <div class="weather-details">
          <div class="weather-temp">)HTML"
           << std::fixed << std::setprecision(1) << info.weather.temperature_celsius
           << "°C / " << info.weather.temperature_fahrenheit << R"HTML(°F</div>
          <div class="weather-condition">)HTML"
           << info.weather.condition << " • " << info.weather.description << R"HTML(</div>
          <div style="margin-top: 10px; font-size: 14px;">
            <svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 4px;" viewBox="0 0 672 672">)HTML" << OlibIcons::DROPLET << R"HTML(</svg> Humidity: )HTML" << std::fixed << std::setprecision(0) << info.weather.humidity_percent
           << R"HTML(% • <svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 4px;" viewBox="0 0 672 672">)HTML" << OlibIcons::GAUGE << R"HTML(</svg> Wind: )HTML" << std::fixed << std::setprecision(1) << info.weather.wind_speed_kmh
           << R"HTML( km/h</div>
        </div>
      </div>
)HTML";
    } else {
      // Show a note if demographics didn't load
      html << R"HTML(
      <p style="color: #999; text-align: center; padding: 20px; margin-top: 20px; background: #f8f9fa; border-radius: 8px;">
        <svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 4px;" viewBox="0 0 672 672">)HTML" << OlibIcons::LIGHTBULB << R"HTML(</svg>
        Location services require <code>libmaxminddb</code> and GeoLite2-City database.<br>
        Install with: <code>brew install libmaxminddb</code>
      </p>
)HTML";
    }
  } else {
    // Demo system unavailable
    html << R"HTML(
      <p style="color: #999; text-align: center; padding: 40px;">
        Demographics system not initialized.
      </p>
)HTML";
  }

  html << R"HTML(
    </div>
)HTML";

  return html.str();
}

std::string OwlHomepage::GenerateLLMStatusCard(OwlBrowserManager* manager) {
  std::ostringstream html;

#if BUILD_WITH_LLAMA
  // Built with on-device LLM
  html << R"HTML(
    <!-- LLM Status Card -->
    <div class="card">
      <h2 class="card-title"><svg style="width: 1.2em; height: 1.2em; display: inline-block; vertical-align: middle; margin-right: 8px;" viewBox="0 0 672 672">)HTML" << OlibIcons::MAGIC_WAND_SPARKLES << R"HTML(</svg>On-Device Intelligence</h2>
)HTML";

  // Check LLM client status through manager
  bool llm_ready = manager && manager->IsLLMAvailable() && manager->IsLLMReady();

  if (llm_ready) {
    html << R"HTML(
      <div class="status-indicator status-online">
        <span class="status-dot"></span>
        LLM Server Online
      </div>
      <div class="stats-grid">
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::MAGIC_WAND_SPARKLES << R"HTML(</div>
          <div class="stat-label">Model</div>
          <div class="stat-value">Qwen3 VL 2B</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::BOLT << R"HTML(</div>
          <div class="stat-label">Acceleration</div>
          <div class="stat-value">Metal GPU</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::GAUGE << R"HTML(</div>
          <div class="stat-label">Inference</div>
          <div class="stat-value">200-800ms</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::LINK << R"HTML(</div>
          <div class="stat-label">Port</div>
          <div class="stat-value">8095</div>
        </div>
      </div>
      <div class="info-row">
        <span class="info-label">Endpoint</span>
        <span class="info-value">http://localhost:8095/v1/chat/completions</span>
      </div>
      <div class="info-row">
        <span class="info-label">Capabilities</span>
        <span class="info-value">Element Classification • Intent Matching • Page Analysis</span>
      </div>
)HTML";
  } else {
    html << R"HTML(
      <div class="status-indicator status-loading">
        <span class="status-dot"></span>
        LLM Server Starting...
      </div>
      <p style="color: #856404; margin-top: 20px;">
        The on-device LLM is initializing. This may take 10-20 seconds on first startup.
      </p>
)HTML";
  }
#else
  // Built without on-device LLM - using external API
  html << R"HTML(
    <!-- LLM Status Card -->
    <div class="card">
      <h2 class="card-title"><svg style="width: 1.2em; height: 1.2em; display: inline-block; vertical-align: middle; margin-right: 8px;" viewBox="0 0 672 672">)HTML" << OlibIcons::MAGIC_WAND_SPARKLES << R"HTML(</svg>LLM Intelligence</h2>
      <div class="status-indicator status-info">
        <span class="status-dot"></span>
        Third-Party LLM
      </div>
      <p style="color: #856404; margin-top: 20px;">
        This build uses external LLM providers. Configure your API provider in <a href="#settings" style="color: #007bff; text-decoration: underline;">LLM Settings</a>.
      </p>
      <div class="info-row">
        <span class="info-label">Supported APIs</span>
        <span class="info-value">OpenAI • Azure • Anthropic • Local Servers</span>
      </div>
)HTML";
#endif

  html << R"HTML(
    </div>
)HTML";

  return html.str();
}

std::string OwlHomepage::GenerateBrowserInfoCard() {
  std::ostringstream html;
  html << R"HTML(
    <!-- Browser Info Card -->
    <div class="card">
      <h2 class="card-title"><svg style="width: 1.2em; height: 1.2em; display: inline-block; vertical-align: middle; margin-right: 8px;" viewBox="0 0 672 672">)HTML" << OlibIcons::GLOBE << R"HTML(</svg>Browser Information</h2>
      <div class="info-row">
        <span class="info-label">Engine</span>
        <span class="info-value">Chromium Embedded Framework (CEF) 140.1.14</span>
      </div>
      <div class="info-row">
        <span class="info-label">Resolution</span>
        <span class="info-value">1920x1080 (Full HD)</span>
      </div>
      <div class="info-row">
        <span class="info-label">Rendering</span>
        <span class="info-value">Off-screen (Windowless) • 30 FPS</span>
      </div>
      <div class="info-row">
        <span class="info-label">Resource Blocker</span>
        <span class="info-value">72 domains (31 ads • 20 analytics • 21 trackers)</span>
      </div>
      <div class="info-row">
        <span class="info-label">Stealth Features</span>
        <span class="info-value">WebDriver removal • Canvas protection • No keychain prompts</span>
      </div>
      <div class="info-row">
        <span class="info-label">AI Features</span>
        <span class="info-value">Semantic matching • Natural language selectors • On-device LLM</span>
      </div>
      <div class="stats-grid" style="margin-top: 20px;">
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::BOLT << R"HTML(</div>
          <div class="stat-label">Cold Start</div>
          <div class="stat-value">&lt;1s</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::DATABASE << R"HTML(</div>
          <div class="stat-label">Memory</div>
          <div class="stat-value">&lt;100MB</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::GAUGE << R"HTML(</div>
          <div class="stat-label">Action Latency</div>
          <div class="stat-value">&lt;30ms</div>
        </div>
        <div class="stat-item">
          <div class="stat-icon">)HTML" << OlibIcons::SHIELD << R"HTML(</div>
          <div class="stat-label">Stealth</div>
          <div class="stat-value">Maximum</div>
        </div>
      </div>
    </div>
)HTML";
  return html.str();
}

std::string OwlHomepage::GenerateFooter(OwlBrowserManager* manager) {
  std::ostringstream html;

  html << R"HTML(
  <script>
    // FontAwesome icon SVGs for use in JavaScript
    const ICON_CHECK = ')HTML" << OlibIcons::CHECK << R"HTML(';
    const ICON_XMARK = ')HTML" << OlibIcons::XMARK << R"HTML(';
    const ICON_ROTATE = ')HTML" << OlibIcons::ARROWS_ROTATE << R"HTML(';
    const ICON_CIRCLE = ')HTML" << OlibIcons::CIRCLE << R"HTML(';
    const ICON_WAND = ')HTML" << OlibIcons::MAGIC_WAND_SPARKLES << R"HTML(';
    const ICON_LIGHTBULB = ')HTML" << OlibIcons::LIGHTBULB << R"HTML(';
    const ICON_GEAR = ')HTML" << OlibIcons::GEAR << R"HTML(';
    const ICON_TRIANGLE_EXCLAMATION = ')HTML" << OlibIcons::TRIANGLE_EXCLAMATION << R"HTML(';

    // Update clock every second
    function updateClock() {
      const timeElement = document.getElementById('current-time');
      if (timeElement) {
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        timeElement.textContent = hours + ':' + minutes + ':' + seconds;
      }
    }

    // Update immediately and then every second
    updateClock();
    setInterval(updateClock, 1000);

    // Task overlay management
    window.taskState = {
      tasks: [],
      currentTaskIndex: 0,
      isExecuting: false
    };

    function showTaskOverlay() {
      document.getElementById('task-overlay').style.display = 'block';
    }

    function hideTaskOverlay() {
      document.getElementById('task-overlay').style.display = 'none';
    }

    function updateTaskProgress() {
      const progress = window.taskState.tasks.length > 0
        ? (window.taskState.currentTaskIndex / window.taskState.tasks.length) * 100
        : 0;
      document.getElementById('task-progress-fill').style.width = progress + '%';
    }

    function renderTasks() {
      const taskList = document.getElementById('task-list');
      if (!taskList) return;

      taskList.innerHTML = window.taskState.tasks.map((task, index) => {
        const status = index < window.taskState.currentTaskIndex ? 'completed' :
                      index === window.taskState.currentTaskIndex ? 'active' :
                      task.failed ? 'failed' : '';
        let icon = '';
        if (status === 'completed') {
          icon = '<svg style="width: 1em; height: 1em;" viewBox="0 0 672 672">' + ICON_CHECK + '</svg>';
        } else if (status === 'active') {
          icon = '<svg style="width: 1em; height: 1em; animation: spin 1s linear infinite;" viewBox="0 0 672 672">' + ICON_ROTATE + '</svg>';
        } else if (status === 'failed') {
          icon = '<svg style="width: 1em; height: 1em;" viewBox="0 0 672 672">' + ICON_XMARK + '</svg>';
        } else {
          icon = '<svg style="width: 1em; height: 1em;" viewBox="0 0 672 672">' + ICON_CIRCLE + '</svg>';
        }
        return '<div class="task-item ' + status + '">' +
               '<div class="task-icon">' + icon + '</div>' +
               '<div>' + task.description + '</div>' +
               '</div>';
      }).join('');

      updateTaskProgress();
    }

    // Execute NLA command
    function executeNLACommand(command) {
      if (!command || window.taskState.isExecuting) return;

      window.taskState.isExecuting = true;
      window.taskState.tasks = [];
      window.taskState.currentTaskIndex = 0;

      showTaskOverlay();

      // Show thinking stage
      document.getElementById('thinking-stage').style.display = 'block';
      document.getElementById('thinking-text').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_WAND + '</svg> AI is analyzing your command...';
      document.getElementById('task-status').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_LIGHTBULB + '</svg> Planning: ' + command;
      document.getElementById('task-list').style.display = 'none';

      // Call injected C++ function via Symbol namespace (stealth)
      const _owl = Symbol.for('owl');
      const ipc = window[_owl] && window[_owl].ipc;
      if (ipc && ipc.nla) {
        ipc.nla(command);
      } else {
        document.getElementById('task-status').textContent = 'Error: NLA bridge not available';
        document.getElementById('thinking-stage').style.display = 'none';
      }
    }

    // Function to update execution progress (called from C++)
    window.updateExecutionProgress = function(stage, data) {
      if (stage === 'planning') {
        document.getElementById('thinking-text').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_WAND + '</svg> Creating action plan...';
      } else if (stage === 'plan_ready') {
        // Show reasoning if available
        if (data.reasoning) {
          document.getElementById('reasoning-content').textContent = data.reasoning;
          document.getElementById('reasoning-box').style.display = 'block';
        }
        // Hide thinking, show task list
        setTimeout(() => {
          document.getElementById('thinking-stage').style.display = 'none';
          document.getElementById('task-list').style.display = 'flex';
          document.getElementById('task-status').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_GEAR + '</svg> Executing actions...';
        }, 800);
      } else if (stage === 'executing') {
        // Task execution started
        document.getElementById('task-status').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_GEAR + '</svg> Executing: ' + data.action;
      } else if (stage === 'completed') {
        document.getElementById('task-status').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_CHECK + '</svg> All tasks completed!';
        setTimeout(() => {
          hideTaskOverlay();
        }, 3000);
      } else if (stage === 'error') {
        document.getElementById('task-status').innerHTML = '<svg style="width: 1em; height: 1em; display: inline-block; vertical-align: middle; margin-right: 6px;" viewBox="0 0 672 672">' + ICON_TRIANGLE_EXCLAMATION + '</svg> Error: ' + data.message;
        document.getElementById('thinking-stage').style.display = 'none';
      }
    };

    // AI Command input handlers
    const commandInput = document.getElementById('ai-command-input');
    const goButton = document.getElementById('execute-command-btn');

    if (commandInput) {
      commandInput.addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
          const command = this.value.trim();
          if (command) {
            executeNLACommand(command);
            this.value = '';
          }
        }
      });
      // Focus on load
      setTimeout(() => commandInput.focus(), 100);
    }

    if (goButton) {
      goButton.addEventListener('click', function() {
        const command = commandInput.value.trim();
        if (command) {
          executeNLACommand(command);
          commandInput.value = '';
        }
      });
    }

    // ===== LLM Provider Management =====

    // In-memory providers list (populated from C++ config on load)
    let _llmProviders = [];
    let _llmConfigLoaded = false;

    // Load providers from memory (populated by C++ callback)
    function loadProviders() {
      return _llmProviders;
    }

    // Save providers to memory and persist to C++
    function saveProviders(providers) {
      _llmProviders = providers;
      // Note: actual persistence happens when activating a provider via sendLLMConfigToBackend
    }

    // Callback handler for C++ to send saved LLM config
    window.onLLMConfigLoaded = function(config) {
      _llmConfigLoaded = true;

      // Store current LLM state for control button updates
      window._currentLLMConfig = config;

      // If there's a saved external config, create a provider for it
      if (config.external_endpoint && config.external_model) {
        // Check if provider already exists
        const existingIndex = _llmProviders.findIndex(p =>
          p.endpoint === config.external_endpoint && p.model === config.external_model
        );

        // Use provider_name if available, otherwise fallback to model name
        const providerName = config.provider_name || config.external_model;

        if (existingIndex === -1) {
          // Add the saved provider
          const provider = {
            name: providerName,
            endpoint: config.external_endpoint,
            model: config.external_model,
            apiKey: config.external_api_key || '',
            active: config.enabled && !config.use_builtin,
            builtin: false,
            isThirdParty: config.is_third_party || false
          };
          _llmProviders.push(provider);
        } else {
          // Update existing provider
          _llmProviders[existingIndex].name = providerName;
          _llmProviders[existingIndex].active = config.enabled && !config.use_builtin;
          _llmProviders[existingIndex].apiKey = config.external_api_key || '';
        }
      }

      // Update UI
      updateActiveProviderDisplay();
      renderProvidersList();

      // Update control buttons based on config state
      if (!config.enabled) {
        updateControlButtons('disabled');
      } else if (config.use_builtin) {
        updateControlButtons('builtin');
      } else if (config.external_endpoint) {
        updateControlButtons('external');
      } else {
        updateControlButtons('none');
      }
    };

    // Request saved config from C++ on page load
    function requestSavedLLMConfig() {
      if (typeof _2 !== 'undefined') {
        _2('load_llm_config');
      }
    }

    // Get active provider
    function getActiveProvider() {
      const providers = loadProviders();
      const active = providers.find(p => p.active);
      )HTML";
#if BUILD_WITH_LLAMA
  html << R"HTML(return active || { name: 'Built-in Model (Qwen3-VL-2B)', endpoint: 'builtin', model: 'Qwen3-VL-2B', active: true, builtin: true };)HTML";
#else
  html << R"HTML(return active || { name: 'No Provider Configured', endpoint: '', model: '', active: false, builtin: false };)HTML";
#endif
  html << R"HTML(
    }

    // Update active provider display
    function updateActiveProviderDisplay() {
      const config = window._currentLLMConfig || {};
      const provider = getActiveProvider();
      const nameEl = document.getElementById('activeProviderName');
      const detailsEl = document.getElementById('activeProviderDetails');
      const badgeEl = document.getElementById('activeBadge');
      const cardEl = document.getElementById('activeProviderCard');

      if (nameEl && detailsEl && badgeEl && cardEl) {
        // Remove all state classes first
        cardEl.classList.remove('no-provider', 'disabled');

        // Check if LLM is disabled
        if (config.enabled === false) {
          nameEl.textContent = 'LLM Disabled';
          detailsEl.textContent = 'AI features are turned off';
          badgeEl.textContent = 'DISABLED';
          badgeEl.classList.remove('hidden');
          cardEl.classList.add('disabled');
          return;
        }

        // Check if using built-in
        if (config.use_builtin) {
          nameEl.textContent = 'Built-in Model (Qwen3-VL-2B)';
          detailsEl.textContent = 'Local on-device model - Fast & Private';
          badgeEl.textContent = 'ACTIVE';
          badgeEl.classList.remove('hidden');
          return;
        }

        // Check external provider
        nameEl.textContent = provider.name;

        // Check if there's actually an active provider
        if (provider.active) {
          // Show active badge and use active card styling
          badgeEl.textContent = 'ACTIVE';
          badgeEl.classList.remove('hidden');

          if (provider.builtin) {
            detailsEl.textContent = 'Local on-device model - Fast & Private';
          } else {
            detailsEl.textContent = `${provider.endpoint} - ${provider.model}`;
          }
        } else {
          // No active provider - hide badge and use warning styling
          badgeEl.classList.add('hidden');
          cardEl.classList.add('no-provider');
          detailsEl.textContent = 'Please add an external LLM provider below';
        }
      }
    }

    // Render providers list
    function renderProvidersList() {
      const providers = loadProviders();
      const listEl = document.getElementById('providersList');

      if (!listEl) return;

      if (providers.length === 0) {
        listEl.innerHTML = '<p style="color: #80868b; text-align: center; padding: 20px;">No providers configured. Add one to get started!</p>';
        return;
      }

      listEl.innerHTML = providers.map((provider, index) => `
        <div class="provider-item ${provider.active ? 'active' : ''}">
          <div class="provider-header">
            <div class="provider-info">
              <h4>${provider.name}</h4>
              <p>${provider.endpoint} - ${provider.model}</p>
            </div>
            <div class="provider-actions">
              ${!provider.active ? `<button class="btn-action btn-activate" onclick="activateProvider(${index})">Activate</button>` : ''}
              <button class="btn-action btn-edit" onclick="editProvider(${index})">Edit</button>
              <button class="btn-action btn-delete" onclick="deleteProvider(${index})">Delete</button>
            </div>
          </div>
        </div>
      `).join('');
    }

    // Show add provider form
    function showAddProviderForm() {
      document.getElementById('formTitle').textContent = 'Add LLM Provider';
      document.getElementById('providerId').value = '';
      document.getElementById('providerNameInput').value = '';
      document.getElementById('providerEndpoint').value = '';
      document.getElementById('providerModel').value = '';
      document.getElementById('providerApiKey').value = '';
      document.getElementById('providerActive').checked = false;

      // Clear test result
      const resultEl = document.getElementById('testResult');
      resultEl.className = 'test-result';
      resultEl.textContent = '';

      document.getElementById('providerForm').style.display = 'block';
    }

    // Test LLM connection - uses C++ backend for reliable HTTP requests
    function testLLMConnection() {
      const endpoint = document.getElementById('providerEndpoint').value.trim();
      const model = document.getElementById('providerModel').value.trim();
      const apiKey = document.getElementById('providerApiKey').value.trim();
      const testButton = event.target;
      const resultEl = document.getElementById('testResult');

      // Validate inputs
      if (!endpoint || !model) {
        resultEl.className = 'test-result error';
        resultEl.textContent = 'Please fill in Endpoint and Model fields first.';
        return;
      }

      // Check if C++ message API is available
      if (typeof _2 === 'undefined') {
        resultEl.className = 'test-result error';
        resultEl.textContent = 'Internal error: message API not available';
        return;
      }

      // Show loading state
      testButton.disabled = true;
      resultEl.className = 'test-result loading';
      resultEl.textContent = 'Testing connection...';

      // Send test request to C++ backend
      _2('test_llm_connection', endpoint, model, apiKey);
    }

    // Callback handler for LLM test results from C++ backend
    window.onLLMTestResult = function(status, message) {
      const resultEl = document.getElementById('testResult');
      const testButton = document.querySelector('button[onclick="testLLMConnection()"]');

      if (testButton) {
        testButton.disabled = false;
      }

      if (status === 'success') {
        resultEl.className = 'test-result success';
        resultEl.innerHTML = '<strong>✓ Connection Successful!</strong><br>' + message;
      } else {
        resultEl.className = 'test-result error';
        resultEl.innerHTML = '<strong>✗ Connection Failed</strong><br>' + message;
      }
    };

    // Cancel provider form
    function cancelProviderForm() {
      document.getElementById('providerForm').style.display = 'none';
      // Clear test result
      const resultEl = document.getElementById('testResult');
      resultEl.className = 'test-result';
      resultEl.textContent = '';
    }

    // Save provider (add or edit)
    // Send active LLM config to C++ backend
    function sendLLMConfigToBackend(provider) {
      const config = {
        enabled: true,
        use_builtin: provider.builtin || false,
        provider_name: provider.name || '',
        external_endpoint: provider.endpoint || '',
        external_model: provider.model || '',
        external_api_key: provider.apiKey || '',
        is_third_party: provider.isThirdParty || false
      };

      const configJson = JSON.stringify(config);

      if (typeof _2 !== 'undefined') {
        _2('save_llm_config', configJson);
      }
    }

    // Use built-in LLM
    function useBuiltinLLM() {
      const config = {
        enabled: true,
        use_builtin: true,
        provider_name: 'Built-in Model',
        external_endpoint: '',
        external_model: '',
        external_api_key: '',
        is_third_party: false
      };

      const configJson = JSON.stringify(config);

      if (typeof _2 !== 'undefined') {
        _2('save_llm_config', configJson);
      }

      // Deactivate all external providers
      const providers = loadProviders();
      providers.forEach(p => p.active = false);
      saveProviders(providers);

      // Update UI
      updateActiveProviderDisplay();
      renderProvidersList();
      updateControlButtons('builtin');

      alert('Switched to built-in LLM. Please restart the browser for changes to take effect.');
    }

    // Disable LLM completely
    function disableLLM() {
      const config = {
        enabled: false,
        use_builtin: false,
        provider_name: '',
        external_endpoint: '',
        external_model: '',
        external_api_key: '',
        is_third_party: false
      };

      const configJson = JSON.stringify(config);

      if (typeof _2 !== 'undefined') {
        _2('save_llm_config', configJson);
      }

      // Deactivate all external providers
      const providers = loadProviders();
      providers.forEach(p => p.active = false);
      saveProviders(providers);

      // Update UI
      updateActiveProviderDisplay();
      renderProvidersList();
      updateControlButtons('disabled');

      alert('LLM disabled. Please restart the browser for changes to take effect.');
    }

    // Update control button states
    function updateControlButtons(state) {
      const btnBuiltin = document.getElementById('btnUseBuiltin');
      const btnDisable = document.getElementById('btnDisableLLM');
      const card = document.getElementById('activeProviderCard');

      // Remove all state classes
      if (btnBuiltin) btnBuiltin.classList.remove('active');
      if (btnDisable) btnDisable.classList.remove('active');
      card.classList.remove('disabled', 'no-provider');

      if (state === 'builtin') {
        if (btnBuiltin) btnBuiltin.classList.add('active');
      } else if (state === 'disabled') {
        if (btnDisable) btnDisable.classList.add('active');
        card.classList.add('disabled');
      } else if (state === 'external') {
        // External provider active - no control button highlighted
      } else if (state === 'none') {
        card.classList.add('no-provider');
      }
    }

    function saveProvider(event) {
      event.preventDefault();

      const providers = loadProviders();
      const index = document.getElementById('providerId').value;
      const provider = {
        name: document.getElementById('providerNameInput').value,
        endpoint: document.getElementById('providerEndpoint').value,
        model: document.getElementById('providerModel').value,
        apiKey: document.getElementById('providerApiKey').value,
        isThirdParty: document.getElementById('providerIsThirdParty').checked,
        active: document.getElementById('providerActive').checked
      };

      if (index === '') {
        // Add new provider
        if (provider.active) {
          // Deactivate all others
          providers.forEach(p => p.active = false);
        }
        providers.push(provider);
      } else {
        // Edit existing provider
        if (provider.active) {
          // Deactivate all others
          providers.forEach(p => p.active = false);
        }
        providers[parseInt(index)] = provider;
      }

      saveProviders(providers);

      // Send active provider config to backend
      if (provider.active) {
        sendLLMConfigToBackend(provider);
      }

      renderProvidersList();
      updateActiveProviderDisplay();
      cancelProviderForm();
    }

    // Edit provider
    function editProvider(index) {
      const providers = loadProviders();
      const provider = providers[index];

      document.getElementById('formTitle').textContent = 'Edit LLM Provider';
      document.getElementById('providerId').value = index;
      document.getElementById('providerNameInput').value = provider.name;
      document.getElementById('providerEndpoint').value = provider.endpoint;
      document.getElementById('providerModel').value = provider.model;
      document.getElementById('providerApiKey').value = provider.apiKey || '';
      document.getElementById('providerIsThirdParty').checked = provider.isThirdParty || false;
      document.getElementById('providerActive').checked = provider.active;

      // Clear test result
      const resultEl = document.getElementById('testResult');
      resultEl.className = 'test-result';
      resultEl.textContent = '';

      document.getElementById('providerForm').style.display = 'block';
    }

    // Delete provider
    function deleteProvider(index) {
      if (!confirm('Are you sure you want to delete this provider?')) {
        return;
      }

      const providers = loadProviders();
      providers.splice(index, 1);
      saveProviders(providers);
      renderProvidersList();
      updateActiveProviderDisplay();
    }

    // Activate provider
    function activateProvider(index) {
      const providers = loadProviders();
      // Deactivate all
      providers.forEach(p => p.active = false);
      // Activate selected
      providers[index].active = true;
      saveProviders(providers);

      // Send active provider config to backend
      sendLLMConfigToBackend(providers[index]);

      renderProvidersList();
      updateActiveProviderDisplay();
    }

    // Initialize providers on settings view load
    window.addEventListener('hashchange', () => {
      if (window.location.hash === '#settings') {
        updateActiveProviderDisplay();
        renderProvidersList();
      }
    });

    // Open Developer Playground in new window
    function openPlaygroundWindow() {
      // Send process message to C++ to open playground window
      if (typeof _2 !== 'undefined') {
        _2('open_playground', '');
      }
    }

    // View switching based on hash
    function showView(viewId) {
      document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
      const view = document.getElementById(viewId);
      if (view) {
        view.classList.add('active');
      }
    }

    // Handle hash changes
    window.addEventListener('hashchange', () => {
      const hash = window.location.hash.slice(1);
      if (hash) {
        showView(hash);
      }
    });

    // Show initial view on load
    window.addEventListener('load', () => {
      const hash = window.location.hash.slice(1) || 'stats';
      showView(hash);

      // Request saved LLM config from C++ backend
      requestSavedLLMConfig();
    });
  </script>
</body>
</html>
)HTML";

  return html.str();
}
