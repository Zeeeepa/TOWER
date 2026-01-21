#include "owl_playground.h"
#include "owl_browser_manager.h"
#include "../resources/icons/icons.h"
#include "logger.h"
#include <sstream>

std::string OwlPlayground::GeneratePlayground(OwlBrowserManager* manager) {
  std::ostringstream html;

  html << GenerateHeader();
  html << GeneratePlaygroundUI();
  html << GenerateFooter();

  return html.str();
}

std::string OwlPlayground::GenerateHeader() {
  return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Developer Playground - Owl Browser</title>
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }

    body {
      font-family: 'SF Mono', 'Monaco', 'Consolas', 'Courier New', monospace;
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
      color: #e8e8e8;
      overflow-x: hidden;
      min-height: 100vh;
    }

    .playground-container {
      display: flex;
      flex-direction: column;
      height: 100vh;
      padding: 16px;
      gap: 16px;
    }

    /* Header */
    .playground-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 16px 24px;
      background: rgba(255, 255, 255, 0.05);
      backdrop-filter: blur(10px);
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.1);
    }

    .header-title {
      display: flex;
      align-items: center;
      gap: 12px;
      font-size: 20px;
      font-weight: 600;
      color: #4285f4;
    }

    .header-title svg {
      width: 28px;
      height: 28px;
      fill: #4285f4;
    }

    .header-actions {
      display: flex;
      gap: 8px;
    }

    /* Main content area */
    .playground-main {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 16px;
      flex: 1;
      overflow: hidden;
    }

    /* Left panel - Test Builder */
    .test-builder {
      display: flex;
      flex-direction: column;
      background: rgba(255, 255, 255, 0.05);
      backdrop-filter: blur(10px);
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.1);
      padding: 20px;
      overflow-y: auto;
      max-height: calc(100vh - 150px);
    }

    .panel-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
      padding-bottom: 12px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    }

    .panel-title {
      font-size: 16px;
      font-weight: 600;
      color: #4285f4;
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .step-count {
      font-size: 12px;
      color: #888;
      font-weight: 500;
    }

    .header-actions {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .btn-sm {
      padding: 6px 12px;
      font-size: 12px;
    }

    /* Step list */
    .steps-container {
      flex: 1;
      overflow-y: auto;
      margin-bottom: 16px;
      min-height: 200px;
      max-height: 500px;
    }

    .step-item {
      display: flex;
      align-items: flex-start;
      gap: 12px;
      padding: 12px;
      background: rgba(255, 255, 255, 0.03);
      border-radius: 8px;
      margin-bottom: 8px;
      border: 1px solid rgba(255, 255, 255, 0.05);
      transition: all 0.2s ease;
      cursor: move;
    }

    .step-item:hover {
      background: rgba(255, 255, 255, 0.08);
      border-color: rgba(66, 133, 244, 0.3);
    }

    .step-item[draggable="true"]:active {
      cursor: grabbing;
    }

    .step-item.step-disabled {
      opacity: 0.5;
      background: rgba(255, 255, 255, 0.01);
    }

    .step-item.step-disabled:hover {
      background: rgba(255, 255, 255, 0.03);
    }

    .step-checkbox {
      width: 18px;
      height: 18px;
      margin-top: 5px;
      cursor: pointer;
      accent-color: #4285f4;
      flex-shrink: 0;
    }

    .step-number {
      display: flex;
      align-items: center;
      justify-content: center;
      width: 28px;
      height: 28px;
      background: rgba(66, 133, 244, 0.2);
      border-radius: 50%;
      font-size: 12px;
      font-weight: 600;
      color: #4285f4;
      flex-shrink: 0;
    }

    .step-content {
      flex: 1;
      display: flex;
      flex-direction: column;
      gap: 6px;
    }

    .step-type {
      font-size: 11px;
      text-transform: uppercase;
      color: #888;
      font-weight: 600;
      letter-spacing: 0.5px;
    }

    .step-description {
      font-size: 13px;
      color: #e8e8e8;
      word-break: break-word;
    }

    .step-params {
      font-size: 12px;
      color: #999;
      font-family: 'Monaco', monospace;
    }

    /* Condition step styling */
    .step-item.step-condition {
      border-left: 3px solid #a78bfa;
    }

    .condition-branch {
      margin-top: 8px;
      padding: 8px;
      border-radius: 6px;
      background: rgba(0, 0, 0, 0.2);
    }

    .condition-branch.condition-true {
      border-left: 2px solid #4ade80;
    }

    .condition-branch.condition-false {
      border-left: 2px solid #f87171;
    }

    .branch-label {
      font-size: 11px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-bottom: 4px;
    }

    .branch-step {
      font-size: 12px;
      color: #ccc;
      padding: 4px 0;
      font-family: 'Monaco', monospace;
    }

    .step-actions {
      display: flex;
      gap: 6px;
      flex-shrink: 0;
    }

    /* Buttons */
    .btn {
      padding: 8px 16px;
      border: none;
      border-radius: 8px;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      gap: 8px;
      white-space: nowrap;
    }

    .btn svg {
      width: 16px;
      height: 16px;
      fill: currentColor;
    }

    /* Only add spacing between icon and text when both exist */
    .btn-text {
      margin-left: 6px;
    }

    .btn-primary {
      background: #4285f4;
      color: white;
    }

    .btn-primary:hover {
      background: #1a73e8;
      transform: translateY(-1px);
    }

    .btn-success {
      background: #34a853;
      color: white;
    }

    .btn-success:hover {
      background: #2d9348;
    }

    .btn-danger {
      background: #ea4335;
      color: white;
    }

    .btn-danger:hover {
      background: #d33426;
    }

    .btn-secondary {
      background: rgba(255, 255, 255, 0.1);
      color: #e8e8e8;
    }

    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.2);
    }

    .btn-icon {
      padding: 8px;
      width: 32px;
      height: 32px;
      display: inline-flex;
      align-items: center;
      justify-content: center;
    }

    .btn-icon svg {
      width: 14px;
      height: 14px;
    }

    /* Add step panel */
    .add-step-panel {
      padding: 16px;
      background: rgba(66, 133, 244, 0.05);
      border-radius: 8px;
      border: 1px dashed rgba(66, 133, 244, 0.3);
      max-height: 300px;
      overflow-y: auto;
    }

    .step-form {
      display: none;
    }

    .step-form.active {
      display: block;
    }

    .form-group {
      margin-bottom: 12px;
    }

    .form-label {
      display: block;
      font-size: 12px;
      color: #999;
      margin-bottom: 6px;
      font-weight: 600;
    }

    .form-input {
      width: 100%;
      padding: 10px 12px;
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 6px;
      color: #e8e8e8;
      font-size: 13px;
      font-family: inherit;
    }

    .form-input:focus {
      outline: none;
      border-color: #4285f4;
      background: rgba(255, 255, 255, 0.08);
    }

    .input-with-picker {
      display: flex;
      gap: 8px;
      align-items: center;
    }

    .input-with-picker .form-input {
      flex: 1;
    }

    .btn-picker {
      flex-shrink: 0;
      width: 40px;
      height: 40px;
      padding: 8px;
      background: rgba(66, 133, 244, 0.1);
      border: 1px solid rgba(66, 133, 244, 0.3);
      border-radius: 6px;
      color: #4285f4;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      justify-content: center;
    }

    .btn-picker:hover {
      background: rgba(66, 133, 244, 0.2);
      border-color: #4285f4;
      transform: scale(1.05);
    }

    .btn-picker svg {
      width: 18px;
      height: 18px;
      fill: currentColor;
    }

    /* Right panel - Code/Preview */
    .preview-panel {
      display: flex;
      flex-direction: column;
      background: rgba(255, 255, 255, 0.05);
      backdrop-filter: blur(10px);
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.1);
      padding: 20px;
      overflow: hidden;
    }

    .tab-buttons {
      display: flex;
      gap: 8px;
    }

    .tab-btn {
      padding: 8px 16px;
      background: transparent;
      border: none;
      border-radius: 6px 6px 0 0;
      color: #999;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s ease;
    }

    .tab-btn.active {
      background: rgba(66, 133, 244, 0.1);
      color: #4285f4;
    }

    .tab-content {
      flex: 1;
      overflow-y: auto;
      display: none;
    }

    .tab-content.active {
      display: block;
    }

    .code-block {
      background: rgba(0, 0, 0, 0.3);
      border-radius: 8px;
      padding: 16px;
      font-family: 'Monaco', 'Consolas', monospace;
      font-size: 12px;
      line-height: 1.6;
      color: #e8e8e8;
      overflow: auto;
      white-space: pre;
      min-height: 200px;
      max-height: 600px;
    }

    /* Status bar */
    .status-bar {
      padding: 12px 20px;
      background: rgba(255, 255, 255, 0.05);
      backdrop-filter: blur(10px);
      border-radius: 8px;
      border: 1px solid rgba(255, 255, 255, 0.1);
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 12px;
    }

    .status-info {
      color: #999;
    }

    .status-badge {
      padding: 4px 12px;
      border-radius: 12px;
      font-size: 11px;
      font-weight: 600;
      text-transform: uppercase;
    }

    .status-idle {
      background: rgba(158, 158, 158, 0.2);
      color: #9e9e9e;
    }

    .status-running {
      background: rgba(66, 133, 244, 0.2);
      color: #4285f4;
    }

    .status-success {
      background: rgba(52, 168, 83, 0.2);
      color: #34a853;
    }

    .status-error {
      background: rgba(234, 67, 53, 0.2);
      color: #ea4335;
    }

    /* Toast Notifications */
    .toast-container {
      position: fixed;
      top: 20px;
      right: 20px;
      z-index: 10000;
      display: flex;
      flex-direction: column;
      gap: 12px;
      max-width: 400px;
    }

    .toast {
      padding: 16px 20px;
      background: rgba(30, 30, 50, 0.95);
      backdrop-filter: blur(10px);
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.1);
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
      display: flex;
      align-items: center;
      gap: 12px;
      animation: slideIn 0.3s ease-out;
      font-size: 14px;
      color: #e8e8e8;
    }

    .toast.success {
      border-left: 3px solid #34a853;
    }

    .toast.error {
      border-left: 3px solid #ea4335;
    }

    .toast.info {
      border-left: 3px solid #4285f4;
    }

    .toast-icon {
      width: 20px;
      height: 20px;
      flex-shrink: 0;
    }

    .toast.success .toast-icon {
      fill: #34a853;
    }

    .toast.error .toast-icon {
      fill: #ea4335;
    }

    .toast.info .toast-icon {
      fill: #4285f4;
    }

    .toast-message {
      flex: 1;
      white-space: pre-wrap;
    }

    @keyframes slideIn {
      from {
        transform: translateX(400px);
        opacity: 0;
      }
      to {
        transform: translateX(0);
        opacity: 1;
      }
    }

    @keyframes slideOut {
      from {
        transform: translateX(0);
        opacity: 1;
      }
      to {
        transform: translateX(400px);
        opacity: 0;
      }
    }

    /* Persistent Error Toast with Close Button */
    .toast.persistent {
      max-width: 450px;
      flex-direction: column;
      align-items: stretch;
      gap: 8px;
    }

    .toast.persistent .toast-header {
      display: flex;
      align-items: center;
      gap: 12px;
      width: 100%;
    }

    .toast.persistent .toast-close {
      margin-left: auto;
      background: rgba(255, 255, 255, 0.1);
      border: none;
      color: #8e8e93;
      width: 24px;
      height: 24px;
      border-radius: 6px;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.15s ease;
      flex-shrink: 0;
    }

    .toast.persistent .toast-close:hover {
      background: rgba(255, 255, 255, 0.2);
      color: #fff;
    }

    .toast.persistent .toast-details {
      background: rgba(0, 0, 0, 0.3);
      border-radius: 8px;
      padding: 12px;
      font-family: 'SF Mono', Monaco, Consolas, monospace;
      font-size: 12px;
      line-height: 1.5;
      max-height: 200px;
      overflow-y: auto;
    }

    .toast.persistent .toast-details-row {
      display: flex;
      gap: 8px;
      margin-bottom: 4px;
    }

    .toast.persistent .toast-details-row:last-child {
      margin-bottom: 0;
    }

    .toast.persistent .toast-details-label {
      color: #8e8e93;
      flex-shrink: 0;
      min-width: 70px;
    }

    .toast.persistent .toast-details-value {
      color: #e8e8e8;
      word-break: break-word;
    }

    /* Firewall Error Type */
    .toast.firewall {
      border-left: 3px solid #ff9500;
    }

    .toast.firewall .toast-icon {
      fill: #ff9500;
    }

    /* Warning Type */
    .toast.warning {
      border-left: 3px solid #ffcc00;
    }

    .toast.warning .toast-icon {
      fill: #ffcc00;
    }

    /* Error Type Badge in Details */
    .toast .error-type-badge {
      display: inline-block;
      padding: 2px 8px;
      border-radius: 4px;
      font-size: 10px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }

    .toast .error-type-badge.firewall {
      background: rgba(255, 149, 0, 0.2);
      color: #ff9500;
    }

    .toast .error-type-badge.navigation {
      background: rgba(234, 67, 53, 0.2);
      color: #ea4335;
    }

    .toast .error-type-badge.element {
      background: rgba(156, 39, 176, 0.2);
      color: #ab47bc;
    }

    .toast .error-type-badge.timeout {
      background: rgba(255, 152, 0, 0.2);
      color: #ff9800;
    }

    .toast .error-type-badge.context {
      background: rgba(96, 125, 139, 0.2);
      color: #78909c;
    }

    /* Scrollbar */
    ::-webkit-scrollbar {
      width: 8px;
      height: 8px;
    }

    ::-webkit-scrollbar-track {
      background: rgba(255, 255, 255, 0.05);
      border-radius: 4px;
    }

    ::-webkit-scrollbar-thumb {
      background: rgba(255, 255, 255, 0.1);
      border-radius: 4px;
    }

    ::-webkit-scrollbar-thumb:hover {
      background: rgba(255, 255, 255, 0.2);
    }

    .empty-state {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 40px 20px;
      color: #666;
      text-align: center;
      gap: 12px;
    }

    .empty-state svg {
      width: 48px;
      height: 48px;
      fill: #444;
      opacity: 0.5;
    }

    .empty-state-text {
      font-size: 14px;
    }

    /* Output Tab Styles */
    .output-container {
      display: flex;
      flex-direction: column;
      gap: 12px;
      padding: 4px;
      max-height: calc(100vh - 280px);
      overflow-y: auto;
    }

    .output-item {
      background: rgba(255, 255, 255, 0.03);
      border-radius: 8px;
      border: 1px solid rgba(255, 255, 255, 0.08);
      overflow: hidden;
    }

    .output-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 14px;
      background: rgba(255, 255, 255, 0.05);
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      cursor: pointer;
    }

    .output-header:hover {
      background: rgba(255, 255, 255, 0.08);
    }

    .output-title {
      display: flex;
      align-items: center;
      gap: 10px;
    }

    .output-step {
      font-size: 11px;
      font-weight: 600;
      color: #4285f4;
      background: rgba(66, 133, 244, 0.15);
      padding: 2px 8px;
      border-radius: 4px;
    }

    .output-type {
      font-size: 12px;
      font-weight: 600;
      color: #e8e8e8;
      text-transform: uppercase;
    }

    .output-timestamp {
      font-size: 11px;
      color: #888;
    }

    .output-actions {
      display: flex;
      gap: 6px;
    }

    .output-body {
      padding: 12px 14px;
      max-height: 400px;
      overflow: auto;
    }

    .output-body.collapsed {
      display: none;
    }

    .output-text {
      font-family: 'Monaco', 'Consolas', monospace;
      font-size: 12px;
      line-height: 1.5;
      color: #e8e8e8;
      white-space: pre-wrap;
      word-break: break-word;
    }

    .output-json {
      font-family: 'Monaco', 'Consolas', monospace;
      font-size: 12px;
      line-height: 1.5;
      white-space: pre-wrap;
      word-break: break-word;
    }

    .output-image {
      max-width: 100%;
      border-radius: 6px;
      border: 1px solid rgba(255, 255, 255, 0.1);
    }

    .output-html, .output-markdown {
      background: rgba(0, 0, 0, 0.2);
      padding: 12px;
      border-radius: 6px;
      font-family: 'Monaco', 'Consolas', monospace;
      font-size: 11px;
      line-height: 1.5;
      max-height: 300px;
      overflow: auto;
      white-space: pre-wrap;
      word-break: break-word;
    }

    .output-count {
      background: #4285f4;
      color: white;
      font-size: 10px;
      font-weight: 600;
      padding: 2px 6px;
      border-radius: 10px;
      margin-left: 6px;
    }

    /* JSON Syntax Highlighting */
    .json-key { color: #9cdcfe; }
    .json-string { color: #ce9178; }
    .json-number { color: #b5cea8; }
    .json-boolean { color: #569cd6; }
    .json-null { color: #569cd6; }

    /* TLD Autocomplete */
    .tld-autocomplete-container {
      position: relative;
    }

    .tld-autocomplete-dropdown {
      position: absolute;
      top: 100%;
      left: 0;
      right: 0;
      background: rgba(30, 30, 30, 0.98);
      border: 1px solid rgba(255, 255, 255, 0.2);
      border-top: none;
      border-radius: 0 0 8px 8px;
      max-height: 200px;
      overflow-y: auto;
      z-index: 1000;
      display: none;
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    }

    .tld-autocomplete-dropdown.show {
      display: block;
    }

    .tld-autocomplete-item {
      padding: 10px 12px;
      cursor: pointer;
      color: #e8e8e8;
      font-size: 13px;
      transition: background 0.15s ease;
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
    }

    .tld-autocomplete-item:last-child {
      border-bottom: none;
    }

    .tld-autocomplete-item:hover,
    .tld-autocomplete-item.active {
      background: rgba(66, 133, 244, 0.2);
    }

    .tld-autocomplete-item .tld-domain {
      color: #4285f4;
      font-weight: 600;
    }

    .tld-autocomplete-item .tld-description {
      font-size: 11px;
      color: #999;
      margin-left: 8px;
    }
  </style>
</head>
)HTML";
}

std::string OwlPlayground::GeneratePlaygroundUI() {
  std::ostringstream html;

  html << R"HTML(
<body>
  <!-- Toast Container -->
  <div class="toast-container" id="toast-container"></div>

  <div class="playground-container">
    <!-- Header -->
    <div class="playground-header">
      <div class="header-title">
        )HTML" << OlibIcons::GAMEPAD << R"HTML(
        <span>Developer Playground</span>
      </div>
      <div class="header-actions">
        <input type="file" id="import-file" accept=".json" style="display: none;" onchange="handleImport(event)">
        <button class="btn btn-secondary" onclick="clearAllSteps()">
          )HTML" << OlibIcons::TRASH << R"HTML(
          Clear All
        </button>
        <button class="btn btn-secondary" onclick="document.getElementById('import-file').click()">
          )HTML" << OlibIcons::ARROW_UP_FROM_BRACKET << R"HTML(
          Import JSON
        </button>
        <button class="btn btn-primary" onclick="exportTest()">
          )HTML" << OlibIcons::CODE << R"HTML(
          Export JSON
        </button>
      </div>
    </div>

    <!-- Main content -->
    <div class="playground-main">
      <!-- Left: Test Builder -->
      <div class="test-builder">
        <div class="panel-header">
          <div class="panel-title">
            Test Steps
            <span id="step-count" class="step-count">0 steps</span>
          </div>
          <div class="header-actions">
            <button class="btn btn-secondary btn-sm" onclick="toggleAllSteps()" id="toggle-all-btn" style="display: none;">
              Select All
            </button>
            <button class="btn btn-primary btn-icon" onclick="addStepPanel()">
              )HTML" << OlibIcons::PLUS << R"HTML(
            </button>
          </div>
        </div>

        <div class="steps-container" id="steps-list">
          <div class="empty-state">
            )HTML" << OlibIcons::CODE << R"HTML(
            <div class="empty-state-text">No steps yet. Click + to add your first step.</div>
          </div>
        </div>

        <div class="add-step-panel" id="add-step-panel" style="display: none;">
          <div class="form-group">
            <label class="form-label">Step Type</label>
            <select class="form-input" id="step-type-select" onchange="selectStepType(this.value)">
              <optgroup label="Control Flow">
                <option value="condition">If Condition</option>
              </optgroup>
              <optgroup label="Navigation">
                <option value="navigate">Navigate</option>
                <option value="reload">Reload Page</option>
                <option value="go_back">Go Back</option>
                <option value="go_forward">Go Forward</option>
              </optgroup>
              <optgroup label="Interaction">
                <option value="click">Click</option>
                <option value="type">Type</option>
                <option value="pick">Pick from Dropdown</option>
                <option value="submit_form">Submit Form</option>
                <option value="press_key">Press Key</option>
                <option value="drag_drop">Drag &amp; Drop (Coordinates)</option>
                <option value="html5_drag_drop">HTML5 Drag &amp; Drop (Selectors)</option>
                <option value="mouse_move">Mouse Move (Human-like)</option>
                <option value="hover">Hover</option>
                <option value="double_click">Double Click</option>
                <option value="right_click">Right Click</option>
                <option value="clear_input">Clear Input</option>
                <option value="focus">Focus Element</option>
                <option value="blur">Blur Element</option>
                <option value="select_all">Select All Text</option>
                <option value="keyboard_combo">Keyboard Combo</option>
                <option value="upload_file">Upload File</option>
              </optgroup>
              <optgroup label="Element State">
                <option value="is_visible">Is Visible</option>
                <option value="is_enabled">Is Enabled</option>
                <option value="is_checked">Is Checked</option>
                <option value="get_attribute">Get Attribute</option>
                <option value="get_bounding_box">Get Bounding Box</option>
              </optgroup>
              <optgroup label="JavaScript">
                <option value="evaluate">Evaluate JavaScript</option>
              </optgroup>
              <optgroup label="Clipboard">
                <option value="clipboard_read">Read Clipboard</option>
                <option value="clipboard_write">Write to Clipboard</option>
                <option value="clipboard_clear">Clear Clipboard</option>
              </optgroup>
              <optgroup label="Frame Handling">
                <option value="list_frames">List Frames</option>
                <option value="switch_to_frame">Switch to Frame</option>
                <option value="switch_to_main_frame">Switch to Main Frame</option>
              </optgroup>
              <optgroup label="Scrolling">
                <option value="scroll_up">Scroll to Top</option>
                <option value="scroll_down">Scroll to Bottom</option>
                <option value="scroll_by">Scroll By Pixels</option>
                <option value="scroll_to_element">Scroll to Element</option>
              </optgroup>
              <optgroup label="Waiting">
                <option value="wait">Wait (ms)</option>
                <option value="wait_for_selector">Wait for Selector</option>
                <option value="wait_for_network_idle">Wait for Network Idle</option>
                <option value="wait_for_function">Wait for Function</option>
                <option value="wait_for_url">Wait for URL</option>
              </optgroup>
              <optgroup label="Extraction">
                <option value="extract">Extract Text</option>
                <option value="get_html">Get HTML</option>
                <option value="get_markdown">Get Markdown</option>
                <option value="extract_json">Extract JSON</option>
                <option value="get_page_info">Get Page Info</option>
              </optgroup>
              <optgroup label="AI Features">
                <option value="query">Query Page (LLM)</option>
                <option value="summarize_page">Summarize Page (LLM)</option>
                <option value="nla">Natural Language Action</option>
              </optgroup>
              <optgroup label="CAPTCHA">
                <option value="detect_captcha">Detect CAPTCHA</option>
                <option value="classify_captcha">Classify CAPTCHA</option>
                <option value="solve_captcha">Solve CAPTCHA</option>
              </optgroup>
              <optgroup label="Cookies">
                <option value="get_cookies">Get Cookies</option>
                <option value="set_cookie">Set Cookie</option>
                <option value="delete_cookies">Delete Cookies</option>
              </optgroup>
              <optgroup label="Visual">
                <option value="screenshot">Screenshot</option>
                <option value="highlight">Highlight Element</option>
                <option value="set_viewport">Set Viewport Size</option>
              </optgroup>
              <optgroup label="Video Recording">
                <option value="record_video">Start Video Recording</option>
                <option value="stop_video">Stop Video Recording</option>
              </optgroup>
              <optgroup label="Network Interception">
                <option value="add_network_rule">Add Network Rule</option>
                <option value="remove_network_rule">Remove Network Rule</option>
                <option value="enable_network_interception">Enable/Disable Interception</option>
                <option value="get_network_log">Get Network Log</option>
                <option value="clear_network_log">Clear Network Log</option>
              </optgroup>
              <optgroup label="File Downloads">
                <option value="set_download_path">Set Download Path</option>
                <option value="get_downloads">Get Downloads</option>
                <option value="wait_for_download">Wait for Download</option>
                <option value="cancel_download">Cancel Download</option>
              </optgroup>
              <optgroup label="Dialog Handling">
                <option value="set_dialog_action">Set Dialog Action</option>
                <option value="get_pending_dialog">Get Pending Dialog</option>
                <option value="handle_dialog">Handle Dialog</option>
                <option value="wait_for_dialog">Wait for Dialog</option>
              </optgroup>
              <optgroup label="Tab Management">
                <option value="new_tab">New Tab</option>
                <option value="get_tabs">Get Tabs</option>
                <option value="switch_tab">Switch Tab</option>
                <option value="get_active_tab">Get Active Tab</option>
                <option value="close_tab">Close Tab</option>
                <option value="get_tab_count">Get Tab Count</option>
                <option value="set_popup_policy">Set Popup Policy</option>
                <option value="get_blocked_popups">Get Blocked Popups</option>
              </optgroup>
            </select>
          </div>

          <!-- Condition form -->
          <div class="step-form" data-type="condition">
            <div class="form-group">
              <label class="form-label">Check Source</label>
              <select class="form-input" id="condition-source" onchange="toggleConditionSourceStep()">
                <option value="previous">Previous Step Result</option>
                <option value="step">Specific Step</option>
              </select>
            </div>
            <div class="form-group" id="condition-source-step-group" style="display: none;">
              <label class="form-label">Step Index</label>
              <input type="number" class="form-input" id="condition-source-step" placeholder="0" min="0">
              <p style="font-size: 11px; color: #888; margin-top: 4px;">Index of the step whose result to check (0-based)</p>
            </div>
            <div class="form-group">
              <label class="form-label">Field Path (optional)</label>
              <input type="text" class="form-input" id="condition-field" placeholder="e.g., success, data.count, text">
              <p style="font-size: 11px; color: #888; margin-top: 4px;">Leave empty to check the entire result</p>
            </div>
            <div class="form-group">
              <label class="form-label">Operator</label>
              <select class="form-input" id="condition-operator" onchange="toggleConditionValue()">
                <option value="is_truthy">Is Truthy</option>
                <option value="is_falsy">Is Falsy</option>
                <option value="is_empty">Is Empty</option>
                <option value="is_not_empty">Is Not Empty</option>
                <option value="equals">Equals</option>
                <option value="not_equals">Not Equals</option>
                <option value="contains">Contains</option>
                <option value="not_contains">Does Not Contain</option>
                <option value="starts_with">Starts With</option>
                <option value="ends_with">Ends With</option>
                <option value="greater_than">Greater Than</option>
                <option value="less_than">Less Than</option>
                <option value="regex_match">Matches Regex</option>
              </select>
            </div>
            <div class="form-group" id="condition-value-group" style="display: none;">
              <label class="form-label">Compare Value</label>
              <input type="text" class="form-input" id="condition-value" placeholder="Value to compare">
            </div>

            <!-- Branch Editor (shown when editing a condition) -->
            <div id="condition-branch-editor" style="display: none; margin-top: 12px;">
              <!-- Then Branch -->
              <div style="background: rgba(74, 222, 128, 0.08); border: 1px solid rgba(74, 222, 128, 0.3); border-radius: 8px; padding: 12px; margin-bottom: 12px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                  <span style="color: #4ade80; font-weight: 500; font-size: 13px;">Then Branch (if true)</span>
                  <button type="button" class="btn btn-success" onclick="openBranchStepModal('onTrue')">)HTML" << OlibIcons::PLUS << R"HTML(<span class="btn-text">Add Step</span></button>
                </div>
                <div id="condition-then-steps" style="min-height: 32px;">
                  <p style="font-size: 12px; color: #888; margin: 0; font-style: italic;">No steps added yet</p>
                </div>
              </div>
              <!-- Else Branch -->
              <div style="background: rgba(248, 113, 113, 0.08); border: 1px solid rgba(248, 113, 113, 0.3); border-radius: 8px; padding: 12px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                  <span style="color: #f87171; font-weight: 500; font-size: 13px;">Else Branch (if false)</span>
                  <button type="button" class="btn btn-danger" onclick="openBranchStepModal('onFalse')">)HTML" << OlibIcons::PLUS << R"HTML(<span class="btn-text">Add Step</span></button>
                </div>
                <div id="condition-else-steps" style="min-height: 32px;">
                  <p style="font-size: 12px; color: #888; margin: 0; font-style: italic;">No steps added yet</p>
                </div>
              </div>
            </div>

            <!-- Info message (shown when adding new condition) -->
            <div id="condition-info-message" style="background: #1a1a2e; border-radius: 8px; padding: 12px; margin-top: 8px;">
              <p style="font-size: 12px; color: #888; margin: 0;">
                <strong style="color: #4ade80;">Then/Else branches:</strong> After adding this condition,
                click the edit button to add steps to the Then and Else branches.
              </p>
            </div>
            <button class="btn btn-success step-submit-btn" style="margin-top: 12px;" onclick="addStep('condition')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Navigate form -->
          <div class="step-form active" data-type="navigate">
            <div class="form-group">
              <label class="form-label">URL</label>
              <div class="tld-autocomplete-container">
                <input type="text" class="form-input" id="navigate-url" placeholder="https://example.com" autocomplete="off">
                <div class="tld-autocomplete-dropdown" id="navigate-tld-dropdown"></div>
              </div>
            </div>
            <button class="btn btn-success step-submit-btn" onclick="addStep('navigate')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Click form -->
          <div class="step-form" data-type="click">
            <div class="form-group">
              <label class="form-label">Selector, Position (e.g., 100x200), or Natural language</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="click-selector" placeholder="search button or 100x200">
                <button class="btn-picker" onclick="startElementPicker('click-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
                <button class="btn-picker" onclick="startPositionPicker('click-selector')" title="Pick position from page">
                  )HTML" << OlibIcons::LOCATION_ARROW << R"HTML(
                </button>
              </div>
            </div>
            <button class="btn btn-success" onclick="addStep('click')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Type form -->
          <div class="step-form" data-type="type">
            <div class="form-group">
              <label class="form-label">Selector, Position (e.g., 100x200), or Natural language</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="type-selector" placeholder="search box or 100x200">
                <button class="btn-picker" onclick="startElementPicker('type-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
                <button class="btn-picker" onclick="startPositionPicker('type-selector')" title="Pick position from page">
                  )HTML" << OlibIcons::LOCATION_ARROW << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Text to type</label>
              <input type="text" class="form-input" id="type-text" placeholder="banana">
            </div>
            <button class="btn btn-success" onclick="addStep('type')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Pick form -->
          <div class="step-form" data-type="pick">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="pick-selector" placeholder="country dropdown">
                <button class="btn-picker" onclick="startElementPicker('pick-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Value to select</label>
              <input type="text" class="form-input" id="pick-value" placeholder="Morocco">
            </div>
            <button class="btn btn-success" onclick="addStep('pick')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait form -->
          <div class="step-form" data-type="wait">
            <div class="form-group">
              <label class="form-label">Duration (milliseconds)</label>
              <input type="number" class="form-input" id="wait-duration" placeholder="2000" value="2000">
            </div>
            <button class="btn btn-success" onclick="addStep('wait')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Screenshot form -->
          <div class="step-form" data-type="screenshot">
            <div class="form-group">
              <label class="form-label">Mode</label>
              <select class="form-input" id="screenshot-mode" onchange="toggleScreenshotSelector()">
                <option value="viewport">Viewport (current visible view)</option>
                <option value="element">Element (specific element)</option>
                <option value="fullpage">Full Page (entire scrollable page)</option>
              </select>
            </div>
            <div class="form-group" id="screenshot-selector-group" style="display: none;">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="screenshot-selector" placeholder="div.profile, #submit-btn">
                <button class="btn-picker" onclick="startElementPicker('screenshot-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Filename (optional)</label>
              <input type="text" class="form-input" id="screenshot-filename" placeholder="screenshot.png">
            </div>
            <button class="btn btn-success" onclick="addStep('screenshot')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Extract form -->
          <div class="step-form" data-type="extract">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="extract-selector" placeholder="body">
                <button class="btn-picker" onclick="startElementPicker('extract-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <button class="btn btn-success" onclick="addStep('extract')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Submit Form form -->
          <div class="step-form" data-type="submit_form">
            <div class="form-group">
              <label class="form-label">Submit the currently focused form by pressing Enter</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">This is useful for search boxes that submit on Enter.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('submit_form')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Query form -->
          <div class="step-form" data-type="query">
            <div class="form-group">
              <label class="form-label">Query</label>
              <input type="text" class="form-input" id="query-text" placeholder="What is the main topic of this page?">
            </div>
            <button class="btn btn-success" onclick="addStep('query')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- NLA form -->
          <div class="step-form" data-type="nla">
            <div class="form-group">
              <label class="form-label">Natural Language Command</label>
              <textarea class="form-input" id="nla-command" rows="3" placeholder="click the first result"></textarea>
            </div>
            <button class="btn btn-success" onclick="addStep('nla')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Solve Captcha form -->
          <div class="step-form" data-type="solve_captcha">
            <div class="form-group">
              <label class="form-label">Auto-detect and solve CAPTCHA on current page</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Automatically detects and solves text-based or image-selection CAPTCHAs.</p>
            </div>
            <div class="form-group">
              <label class="form-label">Provider (optional)</label>
              <select class="form-input" id="captcha-provider">
                <option value="auto">Auto-detect (Recommended)</option>
                <option value="recaptcha">Google reCAPTCHA</option>
                <option value="cloudflare">Cloudflare Turnstile</option>
                <option value="hcaptcha">hCaptcha</option>
                <option value="owl">Owl Test</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label">Max Attempts (optional)</label>
              <input type="number" class="form-input" id="captcha-max-attempts" placeholder="3" min="1" max="10" value="3">
            </div>
            <button class="btn btn-success" onclick="addStep('solve_captcha')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Highlight form -->
          <div class="step-form" data-type="highlight">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="highlight-selector" placeholder="submit button">
                <button class="btn-picker" onclick="startElementPicker('highlight-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <button class="btn btn-success" onclick="addStep('highlight')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Scroll Up form -->
          <div class="step-form" data-type="scroll_up">
            <div class="form-group">
              <label class="form-label">Scroll to top of page</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Instantly scrolls to the top of the current page.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('scroll_up')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Scroll Down form -->
          <div class="step-form" data-type="scroll_down">
            <div class="form-group">
              <label class="form-label">Scroll to bottom of page</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Instantly scrolls to the bottom of the current page.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('scroll_down')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Record Video form -->
          <div class="step-form" data-type="record_video">
            <div class="form-group">
              <label class="form-label">Frame Rate (optional)</label>
              <input type="number" class="form-input" id="video-fps" placeholder="30" value="30">
            </div>
            <button class="btn btn-success" onclick="addStep('record_video')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Stop Video form -->
          <div class="step-form" data-type="stop_video">
            <div class="form-group">
              <label class="form-label">Stop video recording and save</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Stops recording and saves the video to /tmp directory.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('stop_video')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Reload form -->
          <div class="step-form" data-type="reload">
            <div class="form-group">
              <label class="form-label">Reload the current page</label>
              <label class="filter-checkbox" style="margin-top: 8px;">
                <input type="checkbox" id="reload-ignore-cache"> Ignore cache (hard reload)
              </label>
            </div>
            <button class="btn btn-success" onclick="addStep('reload')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Go Back form -->
          <div class="step-form" data-type="go_back">
            <div class="form-group">
              <label class="form-label">Navigate back in browser history</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Goes back to the previous page in history.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('go_back')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Go Forward form -->
          <div class="step-form" data-type="go_forward">
            <div class="form-group">
              <label class="form-label">Navigate forward in browser history</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Goes forward to the next page in history.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('go_forward')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Press Key form -->
          <div class="step-form" data-type="press_key">
            <div class="form-group">
              <label class="form-label">Key to press</label>
              <select class="form-input" id="press-key-value">
                <option value="Enter">Enter</option>
                <option value="Tab">Tab</option>
                <option value="Escape">Escape</option>
                <option value="Backspace">Backspace</option>
                <option value="Delete">Delete</option>
                <option value="ArrowUp">Arrow Up</option>
                <option value="ArrowDown">Arrow Down</option>
                <option value="ArrowLeft">Arrow Left</option>
                <option value="ArrowRight">Arrow Right</option>
                <option value="Space">Space</option>
                <option value="Home">Home</option>
                <option value="End">End</option>
                <option value="PageUp">Page Up</option>
                <option value="PageDown">Page Down</option>
              </select>
            </div>
            <button class="btn btn-success" onclick="addStep('press_key')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Drag & Drop (Coordinates) form -->
          <div class="step-form" data-type="drag_drop">
            <div class="form-group">
              <label class="form-label">Start Position</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="drag-start-pos" placeholder="100x200">
                <button class="btn-picker" onclick="startPositionPicker('drag-start-pos')" title="Pick start position from page">
                  )HTML" << OlibIcons::LOCATION_ARROW << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">End Position</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="drag-end-pos" placeholder="300x200">
                <button class="btn-picker" onclick="startPositionPicker('drag-end-pos')" title="Pick end position from page">
                  )HTML" << OlibIcons::LOCATION_ARROW << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Waypoints (optional, for complex paths)</label>
              <div id="drag-waypoints-container">
                <!-- Waypoints will be added here dynamically -->
              </div>
              <button class="btn btn-secondary btn-sm" onclick="addDragWaypoint()" style="margin-top: 8px;">
                )HTML" << OlibIcons::PLUS << R"HTML(
                Add Waypoint
              </button>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Add waypoints to create complex drag paths (e.g., for drawing or slider CAPTCHAs).</p>
            </div>
            <button class="btn btn-success" onclick="addStep('drag_drop')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- HTML5 Drag & Drop (Selectors) form -->
          <div class="step-form" data-type="html5_drag_drop">
            <div class="form-group">
              <label class="form-label">Source Element (element to drag)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="html5-drag-source" placeholder=".item[data-id='3']">
                <button class="btn-picker" onclick="startElementPicker('html5-drag-source')" title="Pick source element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Target Element (element to drop onto)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="html5-drag-target" placeholder=".item[data-id='1']">
                <button class="btn-picker" onclick="startElementPicker('html5-drag-target')" title="Pick target element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Use this for HTML5 draggable elements (with draggable="true"). For coordinate-based dragging, use "Drag &amp; Drop (Coordinates)".</p>
            <button class="btn btn-success" onclick="addStep('html5_drag_drop')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Mouse Move form -->
          <div class="step-form" data-type="mouse_move">
            <div class="form-group">
              <label class="form-label">Start Position</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="mouse-start-pos" placeholder="100x200">
                <button class="btn-picker" onclick="startPositionPicker('mouse-start-pos')" title="Pick start position from page">
                  )HTML" << OlibIcons::LOCATION_ARROW << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">End Position</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="mouse-end-pos" placeholder="500x300">
                <button class="btn-picker" onclick="startPositionPicker('mouse-end-pos')" title="Pick end position from page">
                  )HTML" << OlibIcons::LOCATION_ARROW << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Steps (0 = auto-calculate)</label>
              <input type="number" class="form-input" id="mouse-steps" placeholder="0" value="0">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Moves cursor along natural bezier curve with human-like jitter. Essential for avoiding bot detection.</p>
            <button class="btn btn-success" onclick="addStep('mouse_move')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Hover form -->
          <div class="step-form" data-type="hover">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="hover-selector" placeholder="menu button or .dropdown-trigger">
                <button class="btn-picker" onclick="startElementPicker('hover-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Duration (ms, optional)</label>
              <input type="number" class="form-input" id="hover-duration" placeholder="100">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Hover over an element to reveal tooltips or dropdown menus.</p>
            <button class="btn btn-success" onclick="addStep('hover')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Double Click form -->
          <div class="step-form" data-type="double_click">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="double-click-selector" placeholder="editable cell or .filename">
                <button class="btn-picker" onclick="startElementPicker('double-click-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Double-click an element (e.g., to edit text or open files).</p>
            <button class="btn btn-success" onclick="addStep('double_click')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Right Click form -->
          <div class="step-form" data-type="right_click">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="right-click-selector" placeholder="file item or .context-target">
                <button class="btn-picker" onclick="startElementPicker('right-click-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Right-click an element to open context menu.</p>
            <button class="btn btn-success" onclick="addStep('right_click')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Clear Input form -->
          <div class="step-form" data-type="clear_input">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="clear-input-selector" placeholder="search input or #email">
                <button class="btn-picker" onclick="startElementPicker('clear-input-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Clear existing text from an input field before typing new value.</p>
            <button class="btn btn-success" onclick="addStep('clear_input')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Focus form -->
          <div class="step-form" data-type="focus">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="focus-selector" placeholder="email input or #password">
                <button class="btn-picker" onclick="startElementPicker('focus-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Focus on an element (triggers focus event).</p>
            <button class="btn btn-success" onclick="addStep('focus')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Blur form -->
          <div class="step-form" data-type="blur">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="blur-selector" placeholder="email input or #password">
                <button class="btn-picker" onclick="startElementPicker('blur-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Remove focus from an element (triggers blur event for validation).</p>
            <button class="btn btn-success" onclick="addStep('blur')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Select All form -->
          <div class="step-form" data-type="select_all">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="select-all-selector" placeholder="text input or #comment">
                <button class="btn-picker" onclick="startElementPicker('select-all-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Select all text in an input element.</p>
            <button class="btn btn-success" onclick="addStep('select_all')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Keyboard Combo form -->
          <div class="step-form" data-type="keyboard_combo">
            <div class="form-group">
              <label class="form-label">Key</label>
              <input type="text" class="form-input" id="keyboard-combo-key" placeholder="a, c, v, z, etc.">
            </div>
            <div class="form-group">
              <label class="form-label">Modifiers (comma-separated)</label>
              <input type="text" class="form-input" id="keyboard-combo-modifiers" placeholder="ctrl, alt, shift, meta">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Press key combinations like Ctrl+A, Ctrl+C, Ctrl+V, etc.</p>
            <button class="btn btn-success" onclick="addStep('keyboard_combo')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Upload File form -->
          <div class="step-form" data-type="upload_file">
            <div class="form-group">
              <label class="form-label">File Input Selector</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="upload-file-selector" placeholder="file input or input[type='file']">
                <button class="btn-picker" onclick="startElementPicker('upload-file-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">File Paths (comma-separated)</label>
              <input type="text" class="form-input" id="upload-file-paths" placeholder="/path/to/file1.pdf, /path/to/file2.jpg">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Upload files to a file input element.</p>
            <button class="btn btn-success" onclick="addStep('upload_file')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Is Visible form -->
          <div class="step-form" data-type="is_visible">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="is-visible-selector" placeholder="login button or .modal">
                <button class="btn-picker" onclick="startElementPicker('is-visible-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Check if an element is visible. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('is_visible')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Is Enabled form -->
          <div class="step-form" data-type="is_enabled">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="is-enabled-selector" placeholder="submit button or #checkout">
                <button class="btn-picker" onclick="startElementPicker('is-enabled-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Check if an element is enabled (not disabled). Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('is_enabled')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Is Checked form -->
          <div class="step-form" data-type="is_checked">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="is-checked-selector" placeholder="agree checkbox or #terms">
                <button class="btn-picker" onclick="startElementPicker('is-checked-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Check if a checkbox or radio button is checked. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('is_checked')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Attribute form -->
          <div class="step-form" data-type="get_attribute">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="get-attribute-selector" placeholder="login link or .item">
                <button class="btn-picker" onclick="startElementPicker('get-attribute-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Attribute Name</label>
              <input type="text" class="form-input" id="get-attribute-name" placeholder="href, data-id, class, etc.">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get an attribute value from an element. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_attribute')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Bounding Box form -->
          <div class="step-form" data-type="get_bounding_box">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="get-bounding-box-selector" placeholder="search button or #submit">
                <button class="btn-picker" onclick="startElementPicker('get-bounding-box-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get the position and size of an element (x, y, width, height). Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_bounding_box')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Evaluate JavaScript form -->
          <div class="step-form" data-type="evaluate">
            <div class="form-group">
              <label class="form-label">JavaScript Code</label>
              <textarea class="form-input" id="evaluate-script" rows="4" placeholder="document.title or document.querySelectorAll('.item').length"></textarea>
            </div>
            <div class="form-group" style="margin-top: 8px;">
              <label class="form-checkbox">
                <input type="checkbox" id="evaluate-return-value">
                <span>Return Value (treat as expression)</span>
              </label>
              <p style="font-size: 10px; color: #666; margin-left: 24px;">Check this if you want to get the result of an expression (e.g., document.title)</p>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Execute JavaScript code in the page context. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('evaluate')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Clipboard Read form -->
          <div class="step-form" data-type="clipboard_read">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Read text content from the system clipboard. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('clipboard_read')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Clipboard Write form -->
          <div class="step-form" data-type="clipboard_write">
            <div class="form-group">
              <label class="form-label">Text to Write</label>
              <textarea class="form-input" id="clipboard-write-text" rows="3" placeholder="Text to copy to clipboard"></textarea>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Write text content to the system clipboard.</p>
            <button class="btn btn-success" onclick="addStep('clipboard_write')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Clipboard Clear form -->
          <div class="step-form" data-type="clipboard_clear">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Clear the system clipboard content.</p>
            <button class="btn btn-success" onclick="addStep('clipboard_clear')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- List Frames form -->
          <div class="step-form" data-type="list_frames">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">List all frames (iframes) on the page. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('list_frames')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Switch to Frame form -->
          <div class="step-form" data-type="switch_to_frame">
            <div class="form-group">
              <label class="form-label">Frame Selector (CSS selector, frame name, or index)</label>
              <input type="text" class="form-input" id="switch-to-frame-selector" placeholder="iframe#payment, payment-frame, or 0">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Switch to an iframe for interaction. Use "Switch to Main Frame" to return.</p>
            <button class="btn btn-success" onclick="addStep('switch_to_frame')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Switch to Main Frame form -->
          <div class="step-form" data-type="switch_to_main_frame">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Switch back to the main frame after working in an iframe.</p>
            <button class="btn btn-success" onclick="addStep('switch_to_main_frame')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Scroll By form -->
          <div class="step-form" data-type="scroll_by">
            <div class="form-group">
              <label class="form-label">Horizontal scroll (pixels)</label>
              <input type="number" class="form-input" id="scroll-by-x" placeholder="0" value="0">
            </div>
            <div class="form-group">
              <label class="form-label">Vertical scroll (pixels)</label>
              <input type="number" class="form-input" id="scroll-by-y" placeholder="500" value="500">
            </div>
            <button class="btn btn-success" onclick="addStep('scroll_by')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Scroll to Element form -->
          <div class="step-form" data-type="scroll_to_element">
            <div class="form-group">
              <label class="form-label">Selector (CSS or natural language)</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="scroll-to-element-selector" placeholder="footer or #contact">
                <button class="btn-picker" onclick="startElementPicker('scroll-to-element-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <button class="btn btn-success" onclick="addStep('scroll_to_element')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait for Selector form -->
          <div class="step-form" data-type="wait_for_selector">
            <div class="form-group">
              <label class="form-label">Selector to wait for</label>
              <div class="input-with-picker">
                <input type="text" class="form-input" id="wait-selector" placeholder=".loading-complete or #results">
                <button class="btn-picker" onclick="startElementPicker('wait-selector')" title="Pick element from page">
                  )HTML" << OlibIcons::HAND << R"HTML(
                </button>
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Timeout (milliseconds)</label>
              <input type="number" class="form-input" id="wait-selector-timeout" placeholder="5000" value="5000">
            </div>
            <button class="btn btn-success" onclick="addStep('wait_for_selector')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait for Network Idle form -->
          <div class="step-form" data-type="wait_for_network_idle">
            <div class="form-group">
              <label class="form-label">Idle Time (milliseconds)</label>
              <input type="number" class="form-input" id="wait-network-idle-time" placeholder="500" value="500">
              <p style="font-size: 11px; color: #888; margin-top: 4px;">Duration of no network activity to consider idle.</p>
            </div>
            <div class="form-group">
              <label class="form-label">Timeout (milliseconds)</label>
              <input type="number" class="form-input" id="wait-network-timeout" placeholder="30000" value="30000">
            </div>
            <button class="btn btn-success" onclick="addStep('wait_for_network_idle')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait for Function form -->
          <div class="step-form" data-type="wait_for_function">
            <div class="form-group">
              <label class="form-label">JavaScript Function</label>
              <textarea class="form-input" id="wait-function-js" rows="3" placeholder="return document.querySelector('.loaded') !== null"></textarea>
              <p style="font-size: 11px; color: #888; margin-top: 4px;">Function body that returns truthy when condition is met.</p>
            </div>
            <div class="form-group">
              <label class="form-label">Polling Interval (milliseconds)</label>
              <input type="number" class="form-input" id="wait-function-polling" placeholder="100" value="100">
            </div>
            <div class="form-group">
              <label class="form-label">Timeout (milliseconds)</label>
              <input type="number" class="form-input" id="wait-function-timeout" placeholder="30000" value="30000">
            </div>
            <button class="btn btn-success" onclick="addStep('wait_for_function')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait for URL form -->
          <div class="step-form" data-type="wait_for_url">
            <div class="form-group">
              <label class="form-label">URL Pattern</label>
              <input type="text" class="form-input" id="wait-url-pattern" placeholder="example.com/success or */checkout/*">
              <p style="font-size: 11px; color: #888; margin-top: 4px;">Substring match or glob pattern with * and ?</p>
            </div>
            <div class="form-group">
              <label class="form-label">Pattern Type</label>
              <select class="form-input" id="wait-url-is-regex">
                <option value="false">Substring Match (default)</option>
                <option value="true">Glob Pattern (* and ?)</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label">Timeout (milliseconds)</label>
              <input type="number" class="form-input" id="wait-url-timeout" placeholder="30000" value="30000">
            </div>
            <button class="btn btn-success" onclick="addStep('wait_for_url')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get HTML form -->
          <div class="step-form" data-type="get_html">
            <div class="form-group">
              <label class="form-label">Cleaning Level</label>
              <select class="form-input" id="get-html-clean-level">
                <option value="basic">Basic (default)</option>
                <option value="minimal">Minimal</option>
                <option value="aggressive">Aggressive</option>
              </select>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('get_html')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Markdown form -->
          <div class="step-form" data-type="get_markdown">
            <div class="form-group">
              <label class="form-label">Get page content as Markdown</label>
              <label class="filter-checkbox" style="margin-top: 8px;">
                <input type="checkbox" id="get-markdown-images" checked> Include images
              </label>
              <label class="filter-checkbox" style="margin-top: 4px;">
                <input type="checkbox" id="get-markdown-links" checked> Include links
              </label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('get_markdown')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Extract JSON form -->
          <div class="step-form" data-type="extract_json">
            <div class="form-group">
              <label class="form-label">Template (optional)</label>
              <select class="form-input" id="extract-json-template">
                <option value="">Auto-detect</option>
                <option value="google_search">Google Search</option>
                <option value="wikipedia">Wikipedia</option>
                <option value="amazon_product">Amazon Product</option>
                <option value="github_repo">GitHub Repo</option>
                <option value="twitter_feed">Twitter Feed</option>
                <option value="reddit_thread">Reddit Thread</option>
              </select>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('extract_json')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Page Info form -->
          <div class="step-form" data-type="get_page_info">
            <div class="form-group">
              <label class="form-label">Get current page information</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Returns URL, title, and navigation state. Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('get_page_info')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Summarize Page form -->
          <div class="step-form" data-type="summarize_page">
            <div class="form-group">
              <label class="form-label">Create LLM summary of current page</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Uses on-device LLM to create a structured summary. Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('summarize_page')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Detect CAPTCHA form -->
          <div class="step-form" data-type="detect_captcha">
            <div class="form-group">
              <label class="form-label">Detect if page has a CAPTCHA</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Uses heuristic analysis to detect CAPTCHAs. Returns confidence score. Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('detect_captcha')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Classify CAPTCHA form -->
          <div class="step-form" data-type="classify_captcha">
            <div class="form-group">
              <label class="form-label">Classify the type of CAPTCHA</label>
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Identifies text-based, image-selection, checkbox, puzzle, audio, or custom CAPTCHA. Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('classify_captcha')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Cookies form -->
          <div class="step-form" data-type="get_cookies">
            <div class="form-group">
              <label class="form-label">URL filter (optional)</label>
              <input type="text" class="form-input" id="get-cookies-url" placeholder="Leave empty for all cookies">
              <p style="font-size: 11px; color: #888; margin-top: 8px;">Returns cookies as JSON. Output will appear in the Output tab.</p>
            </div>
            <button class="btn btn-success" onclick="addStep('get_cookies')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Set Cookie form -->
          <div class="step-form" data-type="set_cookie">
            <div class="form-group">
              <label class="form-label">URL</label>
              <input type="text" class="form-input" id="set-cookie-url" placeholder="https://example.com">
            </div>
            <div class="form-group">
              <label class="form-label">Cookie Name</label>
              <input type="text" class="form-input" id="set-cookie-name" placeholder="session_id">
            </div>
            <div class="form-group">
              <label class="form-label">Cookie Value</label>
              <input type="text" class="form-input" id="set-cookie-value" placeholder="abc123">
            </div>
            <div class="form-group">
              <label class="form-label">Domain (optional)</label>
              <input type="text" class="form-input" id="set-cookie-domain" placeholder=".example.com">
            </div>
            <button class="btn btn-success" onclick="addStep('set_cookie')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Delete Cookies form -->
          <div class="step-form" data-type="delete_cookies">
            <div class="form-group">
              <label class="form-label">URL filter (optional)</label>
              <input type="text" class="form-input" id="delete-cookies-url" placeholder="Leave empty to delete all">
            </div>
            <div class="form-group">
              <label class="form-label">Cookie name (optional)</label>
              <input type="text" class="form-input" id="delete-cookies-name" placeholder="Leave empty to delete all for URL">
            </div>
            <button class="btn btn-success" onclick="addStep('delete_cookies')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Set Viewport form -->
          <div class="step-form" data-type="set_viewport">
            <div class="form-group">
              <label class="form-label">Width (pixels)</label>
              <input type="number" class="form-input" id="viewport-width" placeholder="1280" value="1280">
            </div>
            <div class="form-group">
              <label class="form-label">Height (pixels)</label>
              <input type="number" class="form-input" id="viewport-height" placeholder="720" value="720">
            </div>
            <button class="btn btn-success" onclick="addStep('set_viewport')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- ========== NETWORK INTERCEPTION FORMS ========== -->

          <!-- Add Network Rule form -->
          <div class="step-form" data-type="add_network_rule">
            <div class="form-group">
              <label class="form-label">URL Pattern (glob or regex)</label>
              <input type="text" class="form-input" id="network-rule-pattern" placeholder="*://ads.example.com/*">
            </div>
            <div class="form-group">
              <label class="form-label">Action</label>
              <select class="form-input" id="network-rule-action" onchange="updateNetworkRuleFields()">
                <option value="block">Block</option>
                <option value="allow">Allow</option>
                <option value="mock">Mock Response</option>
                <option value="redirect">Redirect</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label">
                <input type="checkbox" id="network-rule-is-regex"> Use Regex (default is glob)
              </label>
            </div>
            <div class="form-group" id="network-rule-redirect-group" style="display: none;">
              <label class="form-label">Redirect URL</label>
              <input type="text" class="form-input" id="network-rule-redirect-url" placeholder="https://new-api.example.com/">
            </div>
            <div class="form-group" id="network-rule-mock-body-group" style="display: none;">
              <label class="form-label">Mock Response Body</label>
              <textarea class="form-input" id="network-rule-mock-body" rows="3" placeholder='{"status": "ok"}'></textarea>
            </div>
            <div class="form-group" id="network-rule-mock-status-group" style="display: none;">
              <label class="form-label">Mock Status Code</label>
              <input type="number" class="form-input" id="network-rule-mock-status" placeholder="200" value="200">
            </div>
            <div class="form-group" id="network-rule-mock-content-type-group" style="display: none;">
              <label class="form-label">Mock Content-Type</label>
              <input type="text" class="form-input" id="network-rule-mock-content-type" placeholder="application/json">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Block ads, mock API responses, or redirect requests.</p>
            <button class="btn btn-success" onclick="addStep('add_network_rule')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Remove Network Rule form -->
          <div class="step-form" data-type="remove_network_rule">
            <div class="form-group">
              <label class="form-label">Rule ID</label>
              <input type="text" class="form-input" id="remove-network-rule-id" placeholder="rule_000001">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Remove a previously added network rule by its ID.</p>
            <button class="btn btn-success" onclick="addStep('remove_network_rule')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Enable Network Interception form -->
          <div class="step-form" data-type="enable_network_interception">
            <div class="form-group">
              <label class="form-label">Enable Interception</label>
              <select class="form-input" id="network-interception-enabled">
                <option value="true">Enable</option>
                <option value="false">Disable</option>
              </select>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Enable or disable network interception for this context.</p>
            <button class="btn btn-success" onclick="addStep('enable_network_interception')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Network Log form -->
          <div class="step-form" data-type="get_network_log">
            <div class="form-group">
              <label class="form-label">Limit (optional)</label>
              <input type="number" class="form-input" id="network-log-limit" placeholder="Leave empty for all">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get the network request log. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_network_log')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Clear Network Log form -->
          <div class="step-form" data-type="clear_network_log">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Clear the network request log for this context.</p>
            <button class="btn btn-success" onclick="addStep('clear_network_log')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- ========== FILE DOWNLOAD FORMS ========== -->

          <!-- Set Download Path form -->
          <div class="step-form" data-type="set_download_path">
            <div class="form-group">
              <label class="form-label">Download Directory</label>
              <input type="text" class="form-input" id="download-path" placeholder="/tmp/downloads">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Set the directory where files will be downloaded.</p>
            <button class="btn btn-success" onclick="addStep('set_download_path')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Downloads form -->
          <div class="step-form" data-type="get_downloads">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get list of downloads for this context. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_downloads')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait for Download form -->
          <div class="step-form" data-type="wait_for_download">
            <div class="form-group">
              <label class="form-label">Download ID (optional)</label>
              <input type="text" class="form-input" id="wait-download-id" placeholder="Leave empty for any download">
            </div>
            <div class="form-group">
              <label class="form-label">Timeout (milliseconds)</label>
              <input type="number" class="form-input" id="wait-download-timeout" placeholder="30000" value="30000">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Wait for a download to complete.</p>
            <button class="btn btn-success" onclick="addStep('wait_for_download')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Cancel Download form -->
          <div class="step-form" data-type="cancel_download">
            <div class="form-group">
              <label class="form-label">Download ID</label>
              <input type="text" class="form-input" id="cancel-download-id" placeholder="dl_000001">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Cancel a download in progress.</p>
            <button class="btn btn-success" onclick="addStep('cancel_download')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- ========== DIALOG HANDLING FORMS ========== -->

          <!-- Set Dialog Action form -->
          <div class="step-form" data-type="set_dialog_action">
            <div class="form-group">
              <label class="form-label">Dialog Type</label>
              <select class="form-input" id="dialog-type">
                <option value="alert">Alert</option>
                <option value="confirm">Confirm</option>
                <option value="prompt">Prompt</option>
                <option value="beforeunload">Before Unload</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label">Action</label>
              <select class="form-input" id="dialog-action" onchange="updateDialogActionFields()">
                <option value="accept">Accept</option>
                <option value="dismiss">Dismiss</option>
                <option value="accept_with_text">Accept with Text</option>
              </select>
            </div>
            <div class="form-group" id="dialog-prompt-text-group" style="display: none;">
              <label class="form-label">Response Text (for prompts)</label>
              <input type="text" class="form-input" id="dialog-prompt-text" placeholder="My response">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Configure auto-handling policy for JavaScript dialogs.</p>
            <button class="btn btn-success" onclick="addStep('set_dialog_action')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Pending Dialog form -->
          <div class="step-form" data-type="get_pending_dialog">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get information about a pending dialog. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_pending_dialog')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Handle Dialog form -->
          <div class="step-form" data-type="handle_dialog">
            <div class="form-group">
              <label class="form-label">Dialog ID</label>
              <input type="text" class="form-input" id="handle-dialog-id" placeholder="dialog_000001">
            </div>
            <div class="form-group">
              <label class="form-label">Accept or Dismiss</label>
              <select class="form-input" id="handle-dialog-accept">
                <option value="true">Accept</option>
                <option value="false">Dismiss</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label">Response Text (optional, for prompts)</label>
              <input type="text" class="form-input" id="handle-dialog-text" placeholder="My response">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Accept or dismiss a specific pending dialog.</p>
            <button class="btn btn-success" onclick="addStep('handle_dialog')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Wait for Dialog form -->
          <div class="step-form" data-type="wait_for_dialog">
            <div class="form-group">
              <label class="form-label">Timeout (milliseconds)</label>
              <input type="number" class="form-input" id="wait-dialog-timeout" placeholder="5000" value="5000">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Wait for a dialog to appear. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('wait_for_dialog')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- ========== TAB MANAGEMENT FORMS ========== -->

          <!-- New Tab form -->
          <div class="step-form" data-type="new_tab">
            <div class="form-group">
              <label class="form-label">URL (optional)</label>
              <div class="tld-autocomplete-container">
                <input type="text" class="form-input" id="new-tab-url" placeholder="https://google.com" autocomplete="off">
                <div class="tld-autocomplete-dropdown" id="new-tab-tld-dropdown"></div>
              </div>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Create a new tab within this context.</p>
            <button class="btn btn-success" onclick="addStep('new_tab')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Tabs form -->
          <div class="step-form" data-type="get_tabs">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get list of all tabs in this context. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_tabs')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Switch Tab form -->
          <div class="step-form" data-type="switch_tab">
            <div class="form-group">
              <label class="form-label">Tab ID</label>
              <input type="text" class="form-input" id="switch-tab-id" placeholder="tab_000001">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Switch to a specific tab.</p>
            <button class="btn btn-success" onclick="addStep('switch_tab')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Active Tab form -->
          <div class="step-form" data-type="get_active_tab">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get the currently active tab. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_active_tab')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Close Tab form -->
          <div class="step-form" data-type="close_tab">
            <div class="form-group">
              <label class="form-label">Tab ID</label>
              <input type="text" class="form-input" id="close-tab-id" placeholder="tab_000001">
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Close a specific tab. Cannot close the last remaining tab.</p>
            <button class="btn btn-success" onclick="addStep('close_tab')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Tab Count form -->
          <div class="step-form" data-type="get_tab_count">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get the number of tabs in this context. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_tab_count')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Set Popup Policy form -->
          <div class="step-form" data-type="set_popup_policy">
            <div class="form-group">
              <label class="form-label">Popup Policy</label>
              <select class="form-input" id="popup-policy">
                <option value="block">Block</option>
                <option value="allow">Allow</option>
                <option value="new_tab">Open as New Tab</option>
                <option value="background">Open in Background</option>
              </select>
            </div>
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Configure how popups are handled.</p>
            <button class="btn btn-success" onclick="addStep('set_popup_policy')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

          <!-- Get Blocked Popups form -->
          <div class="step-form" data-type="get_blocked_popups">
            <p style="font-size: 11px; color: #888; margin-top: 8px;">Get list of blocked popup URLs. Output will appear in the Output tab.</p>
            <button class="btn btn-success" onclick="addStep('get_blocked_popups')">
              )HTML" << OlibIcons::PLUS << R"HTML(
              <span class="btn-text">Add Step</span>
            </button>
          </div>

        </div>
      </div>

      <!-- Right: Preview/Code/Output -->
      <div class="preview-panel">
        <div class="panel-header">
          <div class="tab-buttons">
            <button class="tab-btn active" data-tab="json" onclick="switchTab('json')">JSON</button>
            <button class="tab-btn" data-tab="javascript" onclick="switchTab('javascript')">JavaScript</button>
            <button class="tab-btn" data-tab="output" onclick="switchTab('output')">Output <span id="output-count" class="output-count" style="display: none;">0</span></button>
          </div>
          <button class="btn btn-secondary btn-sm" onclick="clearOutputs()" id="clear-outputs-btn" style="display: none;">
            )HTML" << OlibIcons::TRASH << R"HTML(
            Clear
          </button>
        </div>

        <div class="tab-content active" id="json-tab">
          <div class="code-block" id="json-code"></div>
        </div>

        <div class="tab-content" id="javascript-tab">
          <div class="code-block" id="javascript-code"></div>
        </div>

        <div class="tab-content" id="output-tab">
          <div id="output-container" class="output-container">
            <div class="empty-state">
              )HTML" << OlibIcons::CODE << R"HTML(
              <div class="empty-state-text">No outputs yet. Run a test with screenshot, extract, or get operations.</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Status Bar -->
    <div class="status-bar">
      <div class="status-info">
        <span id="step-count">0 steps</span> · Ready to execute
      </div>
      <div>
        <span class="status-badge status-idle" id="status-badge">IDLE</span>
        <button class="btn btn-success" style="margin-left: 12px;" onclick="executeTest()">
          )HTML" << OlibIcons::PLAY << R"HTML(
          Execute
        </button>
      </div>
    </div>
  </div>
)HTML";

  return html.str();
}

std::string OwlPlayground::GenerateFooter() {
  std::ostringstream html;
  html << R"HTML(
  <script>
    // Icon SVGs for dynamically generated content
    const ICON_LOCATION_ARROW = ')HTML" << OlibIcons::LOCATION_ARROW << R"HTML(';
    const ICON_TRASH = ')HTML" << OlibIcons::TRASH << R"HTML(';

    let testSteps = [];
    let currentStepType = 'navigate';
    let editingStepIndex = -1;  // -1 means adding new step, >= 0 means editing

    // Toast notification system
    function showToast(message, type = 'info') {
      const container = document.getElementById('toast-container');
      const toast = document.createElement('div');
      toast.className = `toast ${type}`;

      // Icon based on type
      let icon = '';
      if (type === 'success') {
        icon = `<svg class="toast-icon" viewBox="0 0 512 512"><path d="M256 512A256 256 0 1 0 256 0a256 256 0 1 0 0 512zM369 209L241 337c-9.4 9.4-24.6 9.4-33.9 0l-64-64c-9.4-9.4-9.4-24.6 0-33.9s24.6-9.4 33.9 0l47 47L335 175c9.4-9.4 24.6-9.4 33.9 0s9.4 24.6 0 33.9z"/></svg>`;
      } else if (type === 'error') {
        icon = `<svg class="toast-icon" viewBox="0 0 512 512"><path d="M256 512A256 256 0 1 0 256 0a256 256 0 1 0 0 512zm0-384c13.3 0 24 10.7 24 24V264c0 13.3-10.7 24-24 24s-24-10.7-24-24V152c0-13.3 10.7-24 24-24zM224 352a32 32 0 1 1 64 0 32 32 0 1 1 -64 0z"/></svg>`;
      } else {
        icon = `<svg class="toast-icon" viewBox="0 0 512 512"><path d="M256 512A256 256 0 1 0 256 0a256 256 0 1 0 0 512zM216 336h24V272H216c-13.3 0-24-10.7-24-24s10.7-24 24-24h48c13.3 0 24 10.7 24 24v88h8c13.3 0 24 10.7 24 24s-10.7 24-24 24H216c-13.3 0-24-10.7-24-24s10.7-24 24-24zm40-208a32 32 0 1 1 0 64 32 32 0 1 1 0-64z"/></svg>`;
      }

      toast.innerHTML = `
        ${icon}
        <div class="toast-message">${message}</div>
      `;

      container.appendChild(toast);

      // Auto-remove after 3 seconds
      setTimeout(() => {
        toast.style.animation = 'slideOut 0.3s ease-out';
        setTimeout(() => {
          container.removeChild(toast);
        }, 300);
      }, 3000);
    }

    // Persistent error toast with close button and detailed error info
    // errorDetails: { status, message, selector, url, http_status, error_code, step_type, step_index }
    function showPersistentErrorToast(errorDetails) {
      const container = document.getElementById('toast-container');
      const toast = document.createElement('div');

      // Determine error category for styling
      let errorCategory = 'error';
      let errorBadgeClass = 'error';
      let errorBadgeText = 'ERROR';

      const status = errorDetails.status || '';
      if (status === 'firewall_detected') {
        errorCategory = 'firewall';
        errorBadgeClass = 'firewall';
        errorBadgeText = 'FIREWALL';
      } else if (status.includes('navigation') || status === 'page_load_error') {
        errorBadgeClass = 'navigation';
        errorBadgeText = 'NAVIGATION';
      } else if (status.includes('element') || status === 'multiple_elements') {
        errorBadgeClass = 'element';
        errorBadgeText = 'ELEMENT';
      } else if (status.includes('timeout')) {
        errorBadgeClass = 'timeout';
        errorBadgeText = 'TIMEOUT';
      } else if (status.includes('context') || status.includes('browser')) {
        errorBadgeClass = 'context';
        errorBadgeText = 'CONTEXT';
      }

      toast.className = `toast ${errorCategory} persistent`;

      // Icon for error type
      let icon = '';
      if (errorCategory === 'firewall') {
        // Shield icon for firewall
        icon = `<svg class="toast-icon" viewBox="0 0 512 512"><path d="M256 0c4.6 0 9.2 1 13.4 2.9L457.7 82.8c22 9.3 38.4 31 38.3 57.2c-.5 99.2-41.3 280.7-213.6 363.2c-16.7 8-36.1 8-52.8 0C57.3 420.7 16.5 239.2 16 140c-.1-26.2 16.3-47.9 38.3-57.2L242.7 2.9C246.8 1 251.4 0 256 0zm0 66.8V444.8C394 378 431.1 230.1 432 141.4L256 66.8z"/></svg>`;
      } else {
        // Warning icon for other errors
        icon = `<svg class="toast-icon" viewBox="0 0 512 512"><path d="M256 512A256 256 0 1 0 256 0a256 256 0 1 0 0 512zm0-384c13.3 0 24 10.7 24 24V264c0 13.3-10.7 24-24 24s-24-10.7-24-24V152c0-13.3 10.7-24 24-24zM224 352a32 32 0 1 1 64 0 32 32 0 1 1 -64 0z"/></svg>`;
      }

      // Build error title
      let errorTitle = errorDetails.message || 'Test step failed';
      if (errorDetails.step_type) {
        errorTitle = `Step failed: ${errorDetails.step_type}`;
      }

      // Build details section
      let detailsHtml = '';

      if (status) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">Status:</span>
          <span class="toast-details-value"><span class="error-type-badge ${errorBadgeClass}">${errorBadgeText}</span> ${status}</span>
        </div>`;
      }

      if (errorDetails.message) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">Message:</span>
          <span class="toast-details-value">${errorDetails.message}</span>
        </div>`;
      }

      if (errorDetails.selector) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">Selector:</span>
          <span class="toast-details-value">${errorDetails.selector}</span>
        </div>`;
      }

      if (errorDetails.url) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">URL:</span>
          <span class="toast-details-value">${errorDetails.url}</span>
        </div>`;
      }

      if (errorDetails.http_status) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">HTTP:</span>
          <span class="toast-details-value">${errorDetails.http_status}</span>
        </div>`;
      }

      if (errorDetails.error_code) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">Provider:</span>
          <span class="toast-details-value">${errorDetails.error_code}</span>
        </div>`;
      }

      if (errorDetails.step_index !== undefined) {
        detailsHtml += `<div class="toast-details-row">
          <span class="toast-details-label">Step:</span>
          <span class="toast-details-value">#${errorDetails.step_index + 1}</span>
        </div>`;
      }

      toast.innerHTML = `
        <div class="toast-header">
          ${icon}
          <div class="toast-message">${errorTitle}</div>
          <button class="toast-close" onclick="this.closest('.toast').remove()">
            <svg width="12" height="12" viewBox="0 0 12 12" fill="currentColor">
              <path d="M10.5 1.5L1.5 10.5M1.5 1.5l9 9" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            </svg>
          </button>
        </div>
        ${detailsHtml ? `<div class="toast-details">${detailsHtml}</div>` : ''}
      `;

      container.appendChild(toast);
    }

    // Expose to window for C++ calls
    window.showPersistentErrorToast = showPersistentErrorToast;

    // Helper function for network rule form - show/hide fields based on action
    function updateNetworkRuleFields() {
      const action = document.getElementById('network-rule-action').value;
      const redirectGroup = document.getElementById('network-rule-redirect-group');
      const mockBodyGroup = document.getElementById('network-rule-mock-body-group');
      const mockStatusGroup = document.getElementById('network-rule-mock-status-group');
      const mockContentTypeGroup = document.getElementById('network-rule-mock-content-type-group');

      if (redirectGroup) {
        redirectGroup.style.display = action === 'redirect' ? 'block' : 'none';
      }
      if (mockBodyGroup) {
        mockBodyGroup.style.display = action === 'mock' ? 'block' : 'none';
      }
      if (mockStatusGroup) {
        mockStatusGroup.style.display = action === 'mock' ? 'block' : 'none';
      }
      if (mockContentTypeGroup) {
        mockContentTypeGroup.style.display = action === 'mock' ? 'block' : 'none';
      }
    }

    // Helper function for dialog action form - show/hide prompt text field
    function updateDialogActionFields() {
      const action = document.getElementById('dialog-action').value;
      const promptGroup = document.getElementById('dialog-prompt-text-group');

      if (promptGroup) {
        promptGroup.style.display = action === 'accept_with_text' ? 'block' : 'none';
      }
    }

    function selectStepType(type) {
      currentStepType = type;

      // Update dropdown value
      const select = document.getElementById('step-type-select');
      if (select && select.value !== type) {
        select.value = type;
      }

      // Show corresponding form
      document.querySelectorAll('.step-form').forEach(form => {
        if (form.dataset.type === type) {
          form.classList.add('active');
        } else {
          form.classList.remove('active');
        }
      });
    }

    function addStepPanel() {
      const panel = document.getElementById('add-step-panel');
      panel.style.display = panel.style.display === 'none' ? 'block' : 'none';
    }

    function addStep(type) {
      let step = { type: type, selected: true };  // All steps selected by default

      switch(type) {
        case 'navigate':
          step.url = document.getElementById('navigate-url').value;
          document.getElementById('navigate-url').value = '';
          break;
        case 'click':
          step.selector = document.getElementById('click-selector').value;
          document.getElementById('click-selector').value = '';
          break;
        case 'type':
          step.selector = document.getElementById('type-selector').value;
          step.text = document.getElementById('type-text').value;
          document.getElementById('type-selector').value = '';
          document.getElementById('type-text').value = '';
          break;
        case 'pick':
          step.selector = document.getElementById('pick-selector').value;
          step.value = document.getElementById('pick-value').value;
          document.getElementById('pick-selector').value = '';
          document.getElementById('pick-value').value = '';
          break;
        case 'wait':
          step.duration = parseInt(document.getElementById('wait-duration').value) || 2000;
          break;
        case 'screenshot':
          step.mode = document.getElementById('screenshot-mode').value || 'viewport';
          step.selector = document.getElementById('screenshot-selector').value || '';
          step.filename = document.getElementById('screenshot-filename').value || 'screenshot.png';
          // Validate element mode requires selector
          if (step.mode === 'element' && !step.selector) {
            showToast('Element screenshot mode requires a selector', 'error');
            return;
          }
          document.getElementById('screenshot-mode').value = 'viewport';
          document.getElementById('screenshot-selector').value = '';
          document.getElementById('screenshot-filename').value = '';
          document.getElementById('screenshot-selector-group').style.display = 'none';
          break;
        case 'extract':
          step.selector = document.getElementById('extract-selector').value || 'body';
          document.getElementById('extract-selector').value = '';
          break;
        case 'submit_form':
          // No parameters needed for submit_form
          break;
        case 'query':
          step.query = document.getElementById('query-text').value;
          document.getElementById('query-text').value = '';
          break;
        case 'nla':
          step.command = document.getElementById('nla-command').value;
          document.getElementById('nla-command').value = '';
          break;
        case 'solve_captcha':
          step.provider = document.getElementById('captcha-provider').value;
          step.maxAttempts = parseInt(document.getElementById('captcha-max-attempts').value) || 3;
          // Reset form to defaults
          document.getElementById('captcha-provider').value = 'auto';
          document.getElementById('captcha-max-attempts').value = '3';
          break;
        case 'highlight':
          step.selector = document.getElementById('highlight-selector').value;
          document.getElementById('highlight-selector').value = '';
          break;
        case 'scroll_up':
          // No parameters needed for scroll_up
          break;
        case 'scroll_down':
          // No parameters needed for scroll_down
          break;
        case 'record_video':
          step.fps = parseInt(document.getElementById('video-fps').value) || 30;
          break;
        case 'stop_video':
          // No parameters needed for stop_video
          break;
        case 'reload':
          step.ignoreCache = document.getElementById('reload-ignore-cache').checked;
          break;
        case 'go_back':
          // No parameters needed
          break;
        case 'go_forward':
          // No parameters needed
          break;
        case 'press_key':
          step.key = document.getElementById('press-key-value').value;
          break;
        case 'drag_drop':
          const startPos = document.getElementById('drag-start-pos').value;
          const endPos = document.getElementById('drag-end-pos').value;
          if (startPos) {
            const [sx, sy] = startPos.split('x').map(v => parseInt(v.trim()));
            step.startX = sx;
            step.startY = sy;
          }
          if (endPos) {
            const [ex, ey] = endPos.split('x').map(v => parseInt(v.trim()));
            step.endX = ex;
            step.endY = ey;
          }
          // Collect waypoints
          step.midPoints = [];
          const waypointInputs = document.querySelectorAll('.drag-waypoint-input');
          waypointInputs.forEach(input => {
            const val = input.value.trim();
            if (val) {
              const [wx, wy] = val.split('x').map(v => parseInt(v.trim()));
              if (!isNaN(wx) && !isNaN(wy)) {
                step.midPoints.push([wx, wy]);
              }
            }
          });
          // Clear form
          document.getElementById('drag-start-pos').value = '';
          document.getElementById('drag-end-pos').value = '';
          document.getElementById('drag-waypoints-container').innerHTML = '';
          break;
        case 'html5_drag_drop':
          step.sourceSelector = document.getElementById('html5-drag-source').value;
          step.targetSelector = document.getElementById('html5-drag-target').value;
          document.getElementById('html5-drag-source').value = '';
          document.getElementById('html5-drag-target').value = '';
          break;
        case 'mouse_move':
          const mouseStartPos = document.getElementById('mouse-start-pos').value;
          const mouseEndPos = document.getElementById('mouse-end-pos').value;
          if (mouseStartPos) {
            const [msx, msy] = mouseStartPos.split('x').map(v => parseInt(v.trim()));
            step.startX = msx;
            step.startY = msy;
          }
          if (mouseEndPos) {
            const [mex, mey] = mouseEndPos.split('x').map(v => parseInt(v.trim()));
            step.endX = mex;
            step.endY = mey;
          }
          step.steps = parseInt(document.getElementById('mouse-steps').value) || 0;
          document.getElementById('mouse-start-pos').value = '';
          document.getElementById('mouse-end-pos').value = '';
          document.getElementById('mouse-steps').value = '0';
          break;
        case 'hover':
          step.selector = document.getElementById('hover-selector').value;
          step.duration = parseInt(document.getElementById('hover-duration').value) || undefined;
          document.getElementById('hover-selector').value = '';
          document.getElementById('hover-duration').value = '';
          break;
        case 'double_click':
          step.selector = document.getElementById('double-click-selector').value;
          document.getElementById('double-click-selector').value = '';
          break;
        case 'right_click':
          step.selector = document.getElementById('right-click-selector').value;
          document.getElementById('right-click-selector').value = '';
          break;
        case 'clear_input':
          step.selector = document.getElementById('clear-input-selector').value;
          document.getElementById('clear-input-selector').value = '';
          break;
        case 'focus':
          step.selector = document.getElementById('focus-selector').value;
          document.getElementById('focus-selector').value = '';
          break;
        case 'blur':
          step.selector = document.getElementById('blur-selector').value;
          document.getElementById('blur-selector').value = '';
          break;
        case 'select_all':
          step.selector = document.getElementById('select-all-selector').value;
          document.getElementById('select-all-selector').value = '';
          break;
        case 'keyboard_combo':
          step.key = document.getElementById('keyboard-combo-key').value;
          step.modifiers = document.getElementById('keyboard-combo-modifiers').value.split(',').map(m => m.trim()).filter(m => m);
          document.getElementById('keyboard-combo-key').value = '';
          document.getElementById('keyboard-combo-modifiers').value = '';
          break;
        case 'upload_file':
          step.selector = document.getElementById('upload-file-selector').value;
          step.filePaths = document.getElementById('upload-file-paths').value.split(',').map(p => p.trim()).filter(p => p);
          document.getElementById('upload-file-selector').value = '';
          document.getElementById('upload-file-paths').value = '';
          break;
        case 'is_visible':
          step.selector = document.getElementById('is-visible-selector').value;
          document.getElementById('is-visible-selector').value = '';
          break;
        case 'is_enabled':
          step.selector = document.getElementById('is-enabled-selector').value;
          document.getElementById('is-enabled-selector').value = '';
          break;
        case 'is_checked':
          step.selector = document.getElementById('is-checked-selector').value;
          document.getElementById('is-checked-selector').value = '';
          break;
        case 'get_attribute':
          step.selector = document.getElementById('get-attribute-selector').value;
          step.attribute = document.getElementById('get-attribute-name').value;
          document.getElementById('get-attribute-selector').value = '';
          document.getElementById('get-attribute-name').value = '';
          break;
        case 'get_bounding_box':
          step.selector = document.getElementById('get-bounding-box-selector').value;
          document.getElementById('get-bounding-box-selector').value = '';
          break;
        case 'evaluate':
          step.script = document.getElementById('evaluate-script').value;
          step.return_value = document.getElementById('evaluate-return-value').checked;
          document.getElementById('evaluate-script').value = '';
          document.getElementById('evaluate-return-value').checked = false;
          break;
        case 'clipboard_read':
          // No parameters needed
          break;
        case 'clipboard_write':
          step.text = document.getElementById('clipboard-write-text').value;
          document.getElementById('clipboard-write-text').value = '';
          break;
        case 'clipboard_clear':
          // No parameters needed
          break;
        case 'list_frames':
          // No parameters needed
          break;
        case 'switch_to_frame':
          step.frameSelector = document.getElementById('switch-to-frame-selector').value;
          document.getElementById('switch-to-frame-selector').value = '';
          break;
        case 'switch_to_main_frame':
          // No parameters needed
          break;
        case 'scroll_by':
          step.x = parseInt(document.getElementById('scroll-by-x').value) || 0;
          step.y = parseInt(document.getElementById('scroll-by-y').value) || 500;
          break;
        case 'scroll_to_element':
          step.selector = document.getElementById('scroll-to-element-selector').value;
          document.getElementById('scroll-to-element-selector').value = '';
          break;
        case 'wait_for_selector':
          step.selector = document.getElementById('wait-selector').value;
          step.timeout = parseInt(document.getElementById('wait-selector-timeout').value) || 5000;
          document.getElementById('wait-selector').value = '';
          break;
        case 'wait_for_network_idle':
          step.idleTime = parseInt(document.getElementById('wait-network-idle-time').value) || 500;
          step.timeout = parseInt(document.getElementById('wait-network-idle-timeout').value) || 30000;
          break;
        case 'wait_for_function':
          step.function = document.getElementById('wait-function-code').value;
          step.timeout = parseInt(document.getElementById('wait-function-timeout').value) || 30000;
          step.polling = parseInt(document.getElementById('wait-function-polling').value) || 100;
          document.getElementById('wait-function-code').value = '';
          break;
        case 'wait_for_url':
          step.pattern = document.getElementById('wait-url-pattern').value;
          step.matchType = document.getElementById('wait-url-match-type').value || 'substring';
          step.timeout = parseInt(document.getElementById('wait-url-timeout').value) || 30000;
          document.getElementById('wait-url-pattern').value = '';
          break;
        case 'get_html':
          step.cleanLevel = document.getElementById('get-html-clean-level').value;
          break;
        case 'get_markdown':
          step.includeImages = document.getElementById('get-markdown-images').checked;
          step.includeLinks = document.getElementById('get-markdown-links').checked;
          break;
        case 'extract_json':
          step.template = document.getElementById('extract-json-template').value;
          break;
        case 'get_page_info':
          // No parameters needed
          break;
        case 'summarize_page':
          // No parameters needed
          break;
        case 'detect_captcha':
          // No parameters needed
          break;
        case 'classify_captcha':
          // No parameters needed
          break;
        case 'get_cookies':
          step.url = document.getElementById('get-cookies-url').value;
          document.getElementById('get-cookies-url').value = '';
          break;
        case 'set_cookie':
          step.url = document.getElementById('set-cookie-url').value;
          step.name = document.getElementById('set-cookie-name').value;
          step.value = document.getElementById('set-cookie-value').value;
          step.domain = document.getElementById('set-cookie-domain').value;
          document.getElementById('set-cookie-url').value = '';
          document.getElementById('set-cookie-name').value = '';
          document.getElementById('set-cookie-value').value = '';
          document.getElementById('set-cookie-domain').value = '';
          break;
        case 'delete_cookies':
          step.url = document.getElementById('delete-cookies-url').value;
          step.cookieName = document.getElementById('delete-cookies-name').value;
          document.getElementById('delete-cookies-url').value = '';
          document.getElementById('delete-cookies-name').value = '';
          break;
        case 'set_viewport':
          step.width = parseInt(document.getElementById('viewport-width').value) || 1280;
          step.height = parseInt(document.getElementById('viewport-height').value) || 720;
          break;

        // ========== NETWORK INTERCEPTION ==========
        case 'add_network_rule':
          step.urlPattern = document.getElementById('network-rule-pattern').value;
          step.action = document.getElementById('network-rule-action').value;
          step.isRegex = document.getElementById('network-rule-is-regex').checked;
          step.redirectUrl = document.getElementById('network-rule-redirect-url').value;
          step.mockBody = document.getElementById('network-rule-mock-body').value;
          step.mockStatus = parseInt(document.getElementById('network-rule-mock-status').value) || 200;
          step.mockContentType = document.getElementById('network-rule-mock-content-type').value;
          document.getElementById('network-rule-pattern').value = '';
          document.getElementById('network-rule-redirect-url').value = '';
          document.getElementById('network-rule-mock-body').value = '';
          break;
        case 'remove_network_rule':
          step.ruleId = document.getElementById('remove-network-rule-id').value;
          document.getElementById('remove-network-rule-id').value = '';
          break;
        case 'enable_network_interception':
          step.enabled = document.getElementById('network-interception-enabled').value === 'true';
          break;
        case 'get_network_log':
          const limit = document.getElementById('network-log-limit').value;
          step.limit = limit ? parseInt(limit) : 0;
          document.getElementById('network-log-limit').value = '';
          break;
        case 'clear_network_log':
          // No parameters needed
          break;

        // ========== FILE DOWNLOADS ==========
        case 'set_download_path':
          step.path = document.getElementById('download-path').value;
          document.getElementById('download-path').value = '';
          break;
        case 'get_downloads':
          // No parameters needed
          break;
        case 'wait_for_download':
          step.downloadId = document.getElementById('wait-download-id').value;
          step.timeout = parseInt(document.getElementById('wait-download-timeout').value) || 30000;
          document.getElementById('wait-download-id').value = '';
          break;
        case 'cancel_download':
          step.downloadId = document.getElementById('cancel-download-id').value;
          document.getElementById('cancel-download-id').value = '';
          break;

        // ========== DIALOG HANDLING ==========
        case 'set_dialog_action':
          step.dialogType = document.getElementById('dialog-type').value;
          step.action = document.getElementById('dialog-action').value;
          step.promptText = document.getElementById('dialog-prompt-text').value;
          document.getElementById('dialog-prompt-text').value = '';
          break;
        case 'get_pending_dialog':
          // No parameters needed
          break;
        case 'handle_dialog':
          step.dialogId = document.getElementById('handle-dialog-id').value;
          step.accept = document.getElementById('handle-dialog-accept').value === 'true';
          step.responseText = document.getElementById('handle-dialog-text').value;
          document.getElementById('handle-dialog-id').value = '';
          document.getElementById('handle-dialog-text').value = '';
          break;
        case 'wait_for_dialog':
          step.timeout = parseInt(document.getElementById('wait-dialog-timeout').value) || 5000;
          break;

        // ========== TAB MANAGEMENT ==========
        case 'new_tab':
          step.url = document.getElementById('new-tab-url').value;
          document.getElementById('new-tab-url').value = '';
          break;
        case 'get_tabs':
          // No parameters needed
          break;
        case 'switch_tab':
          step.tabId = document.getElementById('switch-tab-id').value;
          document.getElementById('switch-tab-id').value = '';
          break;
        case 'get_active_tab':
          // No parameters needed
          break;
        case 'close_tab':
          step.tabId = document.getElementById('close-tab-id').value;
          document.getElementById('close-tab-id').value = '';
          break;
        case 'get_tab_count':
          // No parameters needed
          break;
        case 'set_popup_policy':
          step.policy = document.getElementById('popup-policy').value;
          break;
        case 'get_blocked_popups':
          // No parameters needed
          break;

        // ========== CONTROL FLOW ==========
        case 'condition':
          step.condition = {
            source: document.getElementById('condition-source').value,
            operator: document.getElementById('condition-operator').value,
          };
          if (step.condition.source === 'step') {
            // Use sourceStepId for compatibility with frontend (stores step index as string ID)
            step.condition.sourceStepId = document.getElementById('condition-source-step').value || '0';
          }
          const fieldValue = document.getElementById('condition-field').value.trim();
          if (fieldValue) {
            step.condition.field = fieldValue;
          }
          const noValueOps = ['is_truthy', 'is_falsy', 'is_empty', 'is_not_empty'];
          if (!noValueOps.includes(step.condition.operator)) {
            step.condition.value = document.getElementById('condition-value').value;
          }
          // Use branches from editor (if editing) or initialize empty
          const branches = window.getConditionBranches ? window.getConditionBranches() : { onTrue: [], onFalse: [] };
          step.onTrue = branches.onTrue.length > 0 ? branches.onTrue : (step.onTrue || []);
          step.onFalse = branches.onFalse.length > 0 ? branches.onFalse : (step.onFalse || []);
          // Reset form
          document.getElementById('condition-source').value = 'previous';
          document.getElementById('condition-source-step').value = '';
          document.getElementById('condition-field').value = '';
          document.getElementById('condition-operator').value = 'is_truthy';
          document.getElementById('condition-value').value = '';
          document.getElementById('condition-source-step-group').style.display = 'none';
          document.getElementById('condition-value-group').style.display = 'none';
          // Reset branch editor
          document.getElementById('condition-branch-editor').style.display = 'none';
          document.getElementById('condition-info-message').style.display = 'block';
          if (window.editingConditionBranches) {
            window.editingConditionBranches = { onTrue: [], onFalse: [] };
          }
          break;
      }

      if (editingStepIndex >= 0) {
        // Update existing step
        testSteps[editingStepIndex] = step;
        editingStepIndex = -1;
        showToast('Step updated successfully', 'success');
      } else {
        // Add new step
        testSteps.push(step);
        showToast(`Step added successfully (Total: ${testSteps.length})`, 'success');
      }

      // Reset button text back to "Add Step" (only main submit buttons, not branch editor buttons)
      document.querySelectorAll('.step-submit-btn .btn-text, .step-form > .btn-success .btn-text').forEach(btn => {
        btn.textContent = 'Add Step';
      });

      // Remove wrapper and restore submit button position
      document.querySelectorAll('.btn-edit-wrapper').forEach(wrapper => {
        const submitBtn = wrapper.querySelector('.step-submit-btn, .btn-success');
        if (submitBtn) {
          submitBtn.style.marginTop = '12px'; // Restore original margin
          wrapper.parentNode.insertBefore(submitBtn, wrapper);
        }
        wrapper.remove();
      });

      renderSteps();
      // Note: renderSteps() already calls updateCode() at the end

      addStepPanel(); // Hide panel after adding
    }

    function renderSteps() {
      const stepsList = document.getElementById('steps-list');

      if (testSteps.length === 0) {
        stepsList.innerHTML = `
          <div class="empty-state">
            )HTML" + std::string(OlibIcons::CODE) + R"HTML(
            <div class="empty-state-text">No steps yet. Click + to add your first step.</div>
          </div>
        `;
        // Still need to update code panel even when empty
        updateCode();
        return;
      }

      stepsList.innerHTML = testSteps.map((step, index) => {
        let description = '';
        let params = '';

        switch(step.type) {
          case 'navigate':
            description = `Navigate to ${step.url}`;
            params = `URL: ${step.url}`;
            break;
          case 'click':
            description = `Click element: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'type':
            description = `Type into ${step.selector}`;
            params = `Text: "${step.text}"`;
            break;
          case 'pick':
            description = `Select from ${step.selector}`;
            params = `Value: "${step.value}"`;
            break;
          case 'wait':
            description = `Wait ${step.duration}ms`;
            params = `Duration: ${step.duration}ms`;
            break;
          case 'screenshot':
            const modeDesc = step.mode === 'fullpage' ? 'full page' : (step.mode === 'element' ? `element (${step.selector})` : 'viewport');
            description = `Take ${modeDesc} screenshot`;
            params = step.mode === 'element' ? `Selector: ${step.selector}, Filename: ${step.filename}` : `Filename: ${step.filename}`;
            break;
          case 'extract':
            description = `Extract text from ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'submit_form':
            description = 'Submit form by pressing Enter';
            params = 'Submits the currently focused form';
            break;
          case 'query':
            description = 'Query page with LLM';
            params = `Query: "${step.query}"`;
            break;
          case 'nla':
            description = 'Execute natural language command';
            params = `Command: "${step.command}"`;
            break;
          case 'solve_captcha':
            description = 'Solve CAPTCHA on page';
            const providerLabel = step.provider === 'auto' ? 'Auto-detect' : step.provider;
            params = `Provider: ${providerLabel}, Max attempts: ${step.maxAttempts || 3}`;
            break;
          case 'highlight':
            description = `Highlight element: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'scroll_up':
            description = 'Scroll to top of page';
            params = 'Scrolls to the very top';
            break;
          case 'scroll_down':
            description = 'Scroll to bottom of page';
            params = 'Scrolls to the very bottom';
            break;
          case 'record_video':
            description = 'Start video recording';
            params = `FPS: ${step.fps || 30}`;
            break;
          case 'stop_video':
            description = 'Stop video recording';
            params = 'Saves video to /tmp directory';
            break;
          case 'reload':
            description = 'Reload current page';
            params = step.ignoreCache ? 'Hard reload (ignore cache)' : 'Normal reload';
            break;
          case 'go_back':
            description = 'Go back in history';
            params = 'Navigate to previous page';
            break;
          case 'go_forward':
            description = 'Go forward in history';
            params = 'Navigate to next page';
            break;
          case 'press_key':
            description = `Press key: ${step.key}`;
            params = `Key: ${step.key}`;
            break;
          case 'drag_drop':
            description = `Drag from ${step.startX}x${step.startY} to ${step.endX}x${step.endY}`;
            params = step.midPoints && step.midPoints.length > 0
              ? `${step.midPoints.length} waypoints`
              : 'Direct drag';
            break;
          case 'html5_drag_drop':
            description = `Drag "${step.sourceSelector}" to "${step.targetSelector}"`;
            params = 'HTML5 drag and drop';
            break;
          case 'mouse_move':
            description = `Move mouse from ${step.startX}x${step.startY} to ${step.endX}x${step.endY}`;
            params = step.steps > 0 ? `${step.steps} steps` : 'Auto steps';
            break;
          case 'hover':
            description = `Hover: ${step.selector}`;
            params = step.duration ? `Duration: ${step.duration}ms` : 'Default duration';
            break;
          case 'double_click':
            description = `Double click: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'right_click':
            description = `Right click: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'clear_input':
            description = `Clear input: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'focus':
            description = `Focus: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'blur':
            description = `Blur: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'select_all':
            description = `Select all: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'keyboard_combo':
            description = `Key combo: ${step.modifiers.join('+')}+${step.key}`;
            params = `Key: ${step.key}, Modifiers: ${step.modifiers.join(', ')}`;
            break;
          case 'upload_file':
            description = `Upload file to: ${step.selector}`;
            params = `Files: ${step.filePaths.length} file(s)`;
            break;
          case 'is_visible':
            description = `Check visible: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'is_enabled':
            description = `Check enabled: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'is_checked':
            description = `Check checked: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'get_attribute':
            description = `Get attribute "${step.attribute}" from: ${step.selector}`;
            params = `Selector: ${step.selector}, Attribute: ${step.attribute}`;
            break;
          case 'get_bounding_box':
            description = `Get bounding box: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'evaluate':
            description = `Evaluate JavaScript${step.return_value ? ' (return value)' : ''}`;
            params = step.script.substring(0, 50) + (step.script.length > 50 ? '...' : '');
            break;
          case 'clipboard_read':
            description = 'Read clipboard';
            params = 'Get text from system clipboard';
            break;
          case 'clipboard_write':
            description = 'Write to clipboard';
            params = step.text ? step.text.substring(0, 50) + (step.text.length > 50 ? '...' : '') : '';
            break;
          case 'clipboard_clear':
            description = 'Clear clipboard';
            params = 'Remove clipboard content';
            break;
          case 'list_frames':
            description = 'List all frames';
            params = 'Get iframe information';
            break;
          case 'switch_to_frame':
            description = `Switch to frame: ${step.frameSelector}`;
            params = `Selector: ${step.frameSelector}`;
            break;
          case 'switch_to_main_frame':
            description = 'Switch to main frame';
            params = 'Return from iframe';
            break;
          case 'scroll_by':
            description = `Scroll by ${step.x}x${step.y} pixels`;
            params = `X: ${step.x}, Y: ${step.y}`;
            break;
          case 'scroll_to_element':
            description = `Scroll to element: ${step.selector}`;
            params = `Selector: ${step.selector}`;
            break;
          case 'wait_for_selector':
            description = `Wait for element: ${step.selector}`;
            params = `Timeout: ${step.timeout}ms`;
            break;
          case 'wait_for_network_idle':
            description = 'Wait for network idle';
            params = `Idle time: ${step.idleTime || 500}ms, Timeout: ${step.timeout || 30000}ms`;
            break;
          case 'wait_for_function':
            description = 'Wait for function';
            params = `Function: ${step.function ? step.function.substring(0, 30) + '...' : 'N/A'}, Timeout: ${step.timeout || 30000}ms`;
            break;
          case 'wait_for_url':
            description = `Wait for URL: ${step.pattern || 'N/A'}`;
            params = `Match type: ${step.matchType || 'substring'}, Timeout: ${step.timeout || 30000}ms`;
            break;
          case 'get_html':
            description = 'Get page HTML';
            params = `Clean level: ${step.cleanLevel || 'basic'}`;
            break;
          case 'get_markdown':
            description = 'Get page as Markdown';
            params = `Images: ${step.includeImages ? 'yes' : 'no'}, Links: ${step.includeLinks ? 'yes' : 'no'}`;
            break;
          case 'extract_json':
            description = 'Extract structured JSON';
            params = step.template ? `Template: ${step.template}` : 'Auto-detect template';
            break;
          case 'get_page_info':
            description = 'Get page information';
            params = 'URL, title, navigation state';
            break;
          case 'summarize_page':
            description = 'Summarize page with LLM';
            params = 'Creates structured summary';
            break;
          case 'detect_captcha':
            description = 'Detect CAPTCHA';
            params = 'Checks if page has CAPTCHA';
            break;
          case 'classify_captcha':
            description = 'Classify CAPTCHA type';
            params = 'Identifies CAPTCHA type';
            break;
          case 'get_cookies':
            description = 'Get cookies';
            params = step.url ? `URL: ${step.url}` : 'All cookies';
            break;
          case 'set_cookie':
            description = `Set cookie: ${step.name}`;
            params = `Value: ${step.value}`;
            break;
          case 'delete_cookies':
            description = 'Delete cookies';
            params = step.url ? `URL: ${step.url}` : 'All cookies';
            break;
          case 'set_viewport':
            description = `Set viewport: ${step.width}x${step.height}`;
            params = `Width: ${step.width}, Height: ${step.height}`;
            break;

          // ========== NETWORK INTERCEPTION ==========
          case 'add_network_rule':
            description = `Add network rule: ${step.action} ${step.urlPattern}`;
            params = step.action === 'mock' ? `Mock ${step.mockStatus}` : (step.action === 'redirect' ? `Redirect to ${step.redirectUrl}` : step.action);
            break;
          case 'remove_network_rule':
            description = `Remove network rule: ${step.ruleId}`;
            params = `Rule ID: ${step.ruleId}`;
            break;
          case 'enable_network_interception':
            description = step.enabled ? 'Enable network interception' : 'Disable network interception';
            params = step.enabled ? 'Interception ON' : 'Interception OFF';
            break;
          case 'get_network_log':
            description = 'Get network log';
            params = step.limit ? `Limit: ${step.limit}` : 'All entries';
            break;
          case 'clear_network_log':
            description = 'Clear network log';
            params = 'Clears all logged requests';
            break;

          // ========== FILE DOWNLOADS ==========
          case 'set_download_path':
            description = `Set download path: ${step.path}`;
            params = `Path: ${step.path}`;
            break;
          case 'get_downloads':
            description = 'Get downloads list';
            params = 'Lists all downloads';
            break;
          case 'wait_for_download':
            description = 'Wait for download';
            params = step.downloadId ? `ID: ${step.downloadId}, Timeout: ${step.timeout}ms` : `Timeout: ${step.timeout}ms`;
            break;
          case 'cancel_download':
            description = `Cancel download: ${step.downloadId}`;
            params = `Download ID: ${step.downloadId}`;
            break;

          // ========== DIALOG HANDLING ==========
          case 'set_dialog_action':
            description = `Set ${step.dialogType} dialog action: ${step.action}`;
            params = step.promptText ? `Text: "${step.promptText}"` : step.action;
            break;
          case 'get_pending_dialog':
            description = 'Get pending dialog';
            params = 'Gets info about pending dialog';
            break;
          case 'handle_dialog':
            description = step.accept ? 'Accept dialog' : 'Dismiss dialog';
            params = `Dialog ID: ${step.dialogId}`;
            break;
          case 'wait_for_dialog':
            description = 'Wait for dialog';
            params = `Timeout: ${step.timeout}ms`;
            break;

          // ========== TAB MANAGEMENT ==========
          case 'new_tab':
            description = step.url ? `New tab: ${step.url}` : 'New empty tab';
            params = step.url ? `URL: ${step.url}` : 'No URL';
            break;
          case 'get_tabs':
            description = 'Get all tabs';
            params = 'Lists all tabs in context';
            break;
          case 'switch_tab':
            description = `Switch to tab: ${step.tabId}`;
            params = `Tab ID: ${step.tabId}`;
            break;
          case 'get_active_tab':
            description = 'Get active tab';
            params = 'Gets current active tab';
            break;
          case 'close_tab':
            description = `Close tab: ${step.tabId}`;
            params = `Tab ID: ${step.tabId}`;
            break;
          case 'get_tab_count':
            description = 'Get tab count';
            params = 'Gets number of tabs';
            break;
          case 'set_popup_policy':
            description = `Set popup policy: ${step.policy}`;
            params = `Policy: ${step.policy}`;
            break;
          case 'get_blocked_popups':
            description = 'Get blocked popups';
            params = 'Lists blocked popup URLs';
            break;

          // ========== CONTROL FLOW ==========
          case 'condition':
            description = `If: ${formatCondition(step.condition)}`;
            const thenCount = step.onTrue ? step.onTrue.length : 0;
            const elseCount = step.onFalse ? step.onFalse.length : 0;
            params = `Then: ${thenCount} step${thenCount !== 1 ? 's' : ''}, Else: ${elseCount} step${elseCount !== 1 ? 's' : ''}`;
            break;
        }

        const isSelected = step.selected !== false;  // Default to true if undefined

        // Special rendering for condition steps with branches
        if (step.type === 'condition') {
          let branchesHtml = '';
          if (step.onTrue && step.onTrue.length > 0) {
            branchesHtml += `<div class="condition-branch condition-true"><div class="branch-label" style="color: #4ade80;">Then:</div>`;
            step.onTrue.forEach((s, i) => {
              branchesHtml += `<div class="branch-step">${i + 1}. ${s.type}: ${formatBranchStep(s)}</div>`;
            });
            branchesHtml += `</div>`;
          }
          if (step.onFalse && step.onFalse.length > 0) {
            branchesHtml += `<div class="condition-branch condition-false"><div class="branch-label" style="color: #f87171;">Else:</div>`;
            step.onFalse.forEach((s, i) => {
              branchesHtml += `<div class="branch-step">${i + 1}. ${s.type}: ${formatBranchStep(s)}</div>`;
            });
            branchesHtml += `</div>`;
          }
          return `
            <div class="step-item step-condition ${isSelected ? '' : 'step-disabled'}" draggable="true" data-index="${index}"
                 ondragstart="handleDragStart(event)"
                 ondragover="handleDragOver(event)"
                 ondrop="handleDrop(event)"
                 ondragend="handleDragEnd(event)">
              <input type="checkbox" class="step-checkbox" ${isSelected ? 'checked' : ''}
                     onchange="toggleStepSelection(${index})"
                     onclick="event.stopPropagation()" title="Select step for execution">
              <div class="step-number">${index + 1}</div>
              <div class="step-content">
                <div class="step-type" style="color: #a78bfa;">CONDITION</div>
                <div class="step-description">${description}</div>
                <div class="step-params">${params}</div>
                ${branchesHtml}
              </div>
              <div class="step-actions">
                <button class="btn btn-secondary btn-icon" onclick="editStep(${index})" title="Edit condition">
                  )HTML" + std::string(OlibIcons::PEN) + R"HTML(
                </button>
                <button class="btn btn-secondary btn-icon" onclick="moveStep(${index}, -1)" ${index === 0 ? 'disabled' : ''} title="Move up">
                  )HTML" + std::string(OlibIcons::ARROW_UP) + R"HTML(
                </button>
                <button class="btn btn-secondary btn-icon" onclick="moveStep(${index}, 1)" ${index === testSteps.length - 1 ? 'disabled' : ''} title="Move down">
                  )HTML" + std::string(OlibIcons::ARROW_DOWN) + R"HTML(
                </button>
                <button class="btn btn-danger btn-icon" onclick="deleteStep(${index})" title="Delete step">
                  )HTML" + std::string(OlibIcons::TRASH) + R"HTML(
                </button>
              </div>
            </div>
          `;
        }

        return `
          <div class="step-item ${isSelected ? '' : 'step-disabled'}" draggable="true" data-index="${index}"
               ondragstart="handleDragStart(event)"
               ondragover="handleDragOver(event)"
               ondrop="handleDrop(event)"
               ondragend="handleDragEnd(event)">
            <input type="checkbox" class="step-checkbox" ${isSelected ? 'checked' : ''}
                   onchange="toggleStepSelection(${index})"
                   onclick="event.stopPropagation()" title="Select step for execution">
            <div class="step-number">${index + 1}</div>
            <div class="step-content">
              <div class="step-type">${step.type.toUpperCase()}</div>
              <div class="step-description">${description}</div>
              <div class="step-params">${params}</div>
            </div>
            <div class="step-actions">
              <button class="btn btn-secondary btn-icon" onclick="editStep(${index})" title="Edit step">
                )HTML" + std::string(OlibIcons::PEN) + R"HTML(
              </button>
              <button class="btn btn-secondary btn-icon" onclick="moveStep(${index}, -1)" ${index === 0 ? 'disabled' : ''} title="Move up">
                )HTML" + std::string(OlibIcons::ARROW_UP) + R"HTML(
              </button>
              <button class="btn btn-secondary btn-icon" onclick="moveStep(${index}, 1)" ${index === testSteps.length - 1 ? 'disabled' : ''} title="Move down">
                )HTML" + std::string(OlibIcons::ARROW_DOWN) + R"HTML(
              </button>
              <button class="btn btn-danger btn-icon" onclick="deleteStep(${index})" title="Delete step">
                )HTML" + std::string(OlibIcons::TRASH) + R"HTML(
              </button>
            </div>
          </div>
        `;
      }).join('');

      // Update step count
      const selectedCount = testSteps.filter(s => s.selected !== false).length;
      const stepCountEl = document.getElementById('step-count');
      if (stepCountEl) {
        if (selectedCount === testSteps.length) {
          stepCountEl.textContent = `${testSteps.length} step${testSteps.length !== 1 ? 's' : ''}`;
        } else {
          stepCountEl.textContent = `${selectedCount}/${testSteps.length} selected`;
        }
      }

      // Show/hide toggle all button
      const toggleBtn = document.getElementById('toggle-all-btn');
      if (toggleBtn) {
        if (testSteps.length > 0) {
          toggleBtn.style.display = 'inline-block';
          toggleBtn.textContent = selectedCount === testSteps.length ? 'Deselect All' : 'Select All';
        } else {
          toggleBtn.style.display = 'none';
        }
      }

      // Update status info
      const statusInfo = document.querySelector('.status-info');
      if (statusInfo && !statusInfo.textContent.includes('Step ')) {
        statusInfo.textContent = `${testSteps.length} step${testSteps.length !== 1 ? 's' : ''} · Ready to execute`;
      }

      // Always update code when steps are rendered
      updateCode();
    }

    function moveStep(index, direction) {
      if (direction === -1 && index > 0) {
        [testSteps[index], testSteps[index - 1]] = [testSteps[index - 1], testSteps[index]];
        renderSteps();
        // Note: renderSteps() already calls updateCode()
      } else if (direction === 1 && index < testSteps.length - 1) {
        [testSteps[index], testSteps[index + 1]] = [testSteps[index + 1], testSteps[index]];
        renderSteps();
        // Note: renderSteps() already calls updateCode()
      }
    }

    function toggleStepSelection(index) {
      testSteps[index].selected = !testSteps[index].selected;
      renderSteps();
    }

    function toggleAllSteps() {
      const selectedCount = testSteps.filter(s => s.selected !== false).length;
      const newState = selectedCount < testSteps.length;  // If not all selected, select all; otherwise deselect all

      testSteps.forEach(step => {
        step.selected = newState;
      });

      renderSteps();
    }

    function editStep(index) {
      const step = testSteps[index];
      editingStepIndex = index;

      // Select the step type
      selectStepType(step.type);

      // Populate form fields based on step type
      switch(step.type) {
        case 'navigate':
          document.getElementById('navigate-url').value = step.url || '';
          break;
        case 'click':
          document.getElementById('click-selector').value = step.selector || '';
          break;
        case 'type':
          document.getElementById('type-selector').value = step.selector || '';
          document.getElementById('type-text').value = step.text || '';
          break;
        case 'pick':
          document.getElementById('pick-selector').value = step.selector || '';
          document.getElementById('pick-value').value = step.value || '';
          break;
        case 'wait':
          document.getElementById('wait-duration').value = step.duration || 2000;
          break;
        case 'screenshot':
          document.getElementById('screenshot-mode').value = step.mode || 'viewport';
          document.getElementById('screenshot-selector').value = step.selector || '';
          document.getElementById('screenshot-filename').value = step.filename || 'screenshot.png';
          toggleScreenshotSelector();
          break;
        case 'extract':
          document.getElementById('extract-selector').value = step.selector || 'body';
          break;
        case 'query':
          document.getElementById('query-text').value = step.query || '';
          break;
        case 'nla':
          document.getElementById('nla-command').value = step.command || '';
          break;
        case 'highlight':
          document.getElementById('highlight-selector').value = step.selector || '';
          break;
        case 'record_video':
          document.getElementById('video-fps').value = step.fps || 30;
          break;
        case 'reload':
          document.getElementById('reload-ignore-cache').checked = step.ignoreCache || false;
          break;
        case 'press_key':
          document.getElementById('press-key-value').value = step.key || 'Enter';
          break;
        case 'solve_captcha':
          document.getElementById('captcha-provider').value = step.provider || 'auto';
          document.getElementById('captcha-max-attempts').value = step.maxAttempts || 3;
          break;
        case 'drag_drop':
          document.getElementById('drag-start-pos').value = step.startX && step.startY ? `${step.startX}x${step.startY}` : '';
          document.getElementById('drag-end-pos').value = step.endX && step.endY ? `${step.endX}x${step.endY}` : '';
          // Clear existing waypoints and recreate
          document.getElementById('drag-waypoints-container').innerHTML = '';
          if (step.midPoints && step.midPoints.length > 0) {
            step.midPoints.forEach((pt, idx) => {
              addDragWaypoint();
              const inputs = document.querySelectorAll('#drag-waypoints-container input');
              if (inputs[idx]) inputs[idx].value = `${pt[0]}x${pt[1]}`;
            });
          }
          break;
        case 'html5_drag_drop':
          document.getElementById('html5-drag-source').value = step.sourceSelector || '';
          document.getElementById('html5-drag-target').value = step.targetSelector || '';
          break;
        case 'mouse_move':
          document.getElementById('mouse-start-pos').value = step.startX && step.startY ? `${step.startX}x${step.startY}` : '';
          document.getElementById('mouse-end-pos').value = step.endX && step.endY ? `${step.endX}x${step.endY}` : '';
          document.getElementById('mouse-steps').value = step.steps || 0;
          break;
        case 'hover':
          document.getElementById('hover-selector').value = step.selector || '';
          document.getElementById('hover-duration').value = step.duration || '';
          break;
        case 'double_click':
          document.getElementById('double-click-selector').value = step.selector || '';
          break;
        case 'right_click':
          document.getElementById('right-click-selector').value = step.selector || '';
          break;
        case 'clear_input':
          document.getElementById('clear-input-selector').value = step.selector || '';
          break;
        case 'focus':
          document.getElementById('focus-selector').value = step.selector || '';
          break;
        case 'blur':
          document.getElementById('blur-selector').value = step.selector || '';
          break;
        case 'select_all':
          document.getElementById('select-all-selector').value = step.selector || '';
          break;
        case 'keyboard_combo':
          document.getElementById('keyboard-combo-key').value = step.key || '';
          document.getElementById('keyboard-combo-modifiers').value = (step.modifiers || []).join(', ');
          break;
        case 'upload_file':
          document.getElementById('upload-file-selector').value = step.selector || '';
          document.getElementById('upload-file-paths').value = (step.filePaths || []).join(', ');
          break;
        case 'is_visible':
          document.getElementById('is-visible-selector').value = step.selector || '';
          break;
        case 'is_enabled':
          document.getElementById('is-enabled-selector').value = step.selector || '';
          break;
        case 'is_checked':
          document.getElementById('is-checked-selector').value = step.selector || '';
          break;
        case 'get_attribute':
          document.getElementById('get-attribute-selector').value = step.selector || '';
          document.getElementById('get-attribute-name').value = step.attribute || '';
          break;
        case 'get_bounding_box':
          document.getElementById('get-bounding-box-selector').value = step.selector || '';
          break;
        case 'evaluate':
          document.getElementById('evaluate-script').value = step.script || '';
          document.getElementById('evaluate-return-value').checked = step.return_value || false;
          break;
        case 'clipboard_read':
          // No parameters
          break;
        case 'clipboard_write':
          document.getElementById('clipboard-write-text').value = step.text || '';
          break;
        case 'clipboard_clear':
          // No parameters
          break;
        case 'list_frames':
          // No parameters
          break;
        case 'switch_to_frame':
          document.getElementById('switch-to-frame-selector').value = step.frameSelector || '';
          break;
        case 'switch_to_main_frame':
          // No parameters
          break;
        case 'scroll_by':
          document.getElementById('scroll-by-x').value = step.x || 0;
          document.getElementById('scroll-by-y').value = step.y || 500;
          break;
        case 'scroll_to_element':
          document.getElementById('scroll-to-element-selector').value = step.selector || '';
          break;
        case 'wait_for_selector':
          document.getElementById('wait-selector').value = step.selector || '';
          document.getElementById('wait-selector-timeout').value = step.timeout || 5000;
          break;
        case 'wait_for_network_idle':
          document.getElementById('wait-network-idle-time').value = step.idleTime || 500;
          document.getElementById('wait-network-idle-timeout').value = step.timeout || 30000;
          break;
        case 'wait_for_function':
          document.getElementById('wait-function-code').value = step.function || '';
          document.getElementById('wait-function-timeout').value = step.timeout || 30000;
          document.getElementById('wait-function-polling').value = step.polling || 100;
          break;
        case 'wait_for_url':
          document.getElementById('wait-url-pattern').value = step.pattern || '';
          document.getElementById('wait-url-match-type').value = step.matchType || 'substring';
          document.getElementById('wait-url-timeout').value = step.timeout || 30000;
          break;
        case 'get_html':
          document.getElementById('get-html-clean-level').value = step.cleanLevel || 'basic';
          break;
        case 'get_markdown':
          document.getElementById('get-markdown-images').checked = step.includeImages !== false;
          document.getElementById('get-markdown-links').checked = step.includeLinks !== false;
          break;
        case 'extract_json':
          document.getElementById('extract-json-template').value = step.template || '';
          break;
        case 'get_cookies':
          document.getElementById('get-cookies-url').value = step.url || '';
          break;
        case 'set_cookie':
          document.getElementById('set-cookie-url').value = step.url || '';
          document.getElementById('set-cookie-name').value = step.name || '';
          document.getElementById('set-cookie-value').value = step.value || '';
          document.getElementById('set-cookie-domain').value = step.domain || '';
          break;
        case 'delete_cookies':
          document.getElementById('delete-cookies-url').value = step.url || '';
          document.getElementById('delete-cookies-name').value = step.cookieName || '';
          break;
        case 'set_viewport':
          document.getElementById('viewport-width').value = step.width || 1280;
          document.getElementById('viewport-height').value = step.height || 720;
          break;

        // ========== NETWORK INTERCEPTION ==========
        case 'add_network_rule':
          document.getElementById('network-rule-pattern').value = step.urlPattern || '';
          document.getElementById('network-rule-action').value = step.action || 'block';
          document.getElementById('network-rule-is-regex').checked = step.isRegex || false;
          document.getElementById('network-rule-redirect-url').value = step.redirectUrl || '';
          document.getElementById('network-rule-mock-body').value = step.mockBody || '';
          document.getElementById('network-rule-mock-status').value = step.mockStatus || 200;
          document.getElementById('network-rule-mock-content-type').value = step.mockContentType || '';
          updateNetworkRuleFields();
          break;
        case 'remove_network_rule':
          document.getElementById('remove-network-rule-id').value = step.ruleId || '';
          break;
        case 'enable_network_interception':
          document.getElementById('network-interception-enabled').value = step.enabled ? 'true' : 'false';
          break;
        case 'get_network_log':
          document.getElementById('network-log-limit').value = step.limit || '';
          break;
        case 'clear_network_log':
          // No parameters
          break;

        // ========== FILE DOWNLOADS ==========
        case 'set_download_path':
          document.getElementById('download-path').value = step.path || '';
          break;
        case 'get_downloads':
          // No parameters
          break;
        case 'wait_for_download':
          document.getElementById('wait-download-id').value = step.downloadId || '';
          document.getElementById('wait-download-timeout').value = step.timeout || 30000;
          break;
        case 'cancel_download':
          document.getElementById('cancel-download-id').value = step.downloadId || '';
          break;

        // ========== DIALOG HANDLING ==========
        case 'set_dialog_action':
          document.getElementById('dialog-type').value = step.dialogType || 'alert';
          document.getElementById('dialog-action').value = step.action || 'accept';
          document.getElementById('dialog-prompt-text').value = step.promptText || '';
          updateDialogActionFields();
          break;
        case 'get_pending_dialog':
          // No parameters
          break;
        case 'handle_dialog':
          document.getElementById('handle-dialog-id').value = step.dialogId || '';
          document.getElementById('handle-dialog-accept').value = step.accept ? 'true' : 'false';
          document.getElementById('handle-dialog-text').value = step.responseText || '';
          break;
        case 'wait_for_dialog':
          document.getElementById('wait-dialog-timeout').value = step.timeout || 5000;
          break;

        // ========== TAB MANAGEMENT ==========
        case 'new_tab':
          document.getElementById('new-tab-url').value = step.url || '';
          break;
        case 'get_tabs':
          // No parameters
          break;
        case 'switch_tab':
          document.getElementById('switch-tab-id').value = step.tabId || '';
          break;
        case 'get_active_tab':
          // No parameters
          break;
        case 'close_tab':
          document.getElementById('close-tab-id').value = step.tabId || '';
          break;
        case 'get_tab_count':
          // No parameters
          break;
        case 'set_popup_policy':
          document.getElementById('popup-policy').value = step.policy || 'block';
          break;
        case 'get_blocked_popups':
          // No parameters
          break;

        // ========== CONTROL FLOW ==========
        case 'condition':
          if (step.condition) {
            document.getElementById('condition-source').value = step.condition.source || 'previous';
            // Support both sourceStepId (frontend format) and sourceStepIndex (legacy)
            document.getElementById('condition-source-step').value = step.condition.sourceStepId || step.condition.sourceStepIndex || '';
            document.getElementById('condition-field').value = step.condition.field || '';
            document.getElementById('condition-operator').value = step.condition.operator || 'is_truthy';
            document.getElementById('condition-value').value = step.condition.value || '';
            toggleConditionSourceStep();
            toggleConditionValue();
          }
          break;
      }

      // Update button text to "Update Step" (only main submit buttons, not branch editor buttons)
      document.querySelectorAll('.step-submit-btn .btn-text, .step-form > .btn-success .btn-text').forEach(btn => {
        btn.textContent = 'Update Step';
      });

      // Add cancel button if not already present
      const activeForm = document.querySelector('.step-form.active');
      if (activeForm && !activeForm.querySelector('.btn-cancel-edit')) {
        // Use step-submit-btn to find the main submit button (not branch editor buttons)
        const submitBtn = activeForm.querySelector('.step-submit-btn') || activeForm.querySelector(':scope > .btn-success');
        if (submitBtn) {
          // Create a wrapper div for proper button alignment
          const wrapper = document.createElement('div');
          wrapper.className = 'btn-edit-wrapper';
          wrapper.style.display = 'flex';
          wrapper.style.alignItems = 'center';
          wrapper.style.gap = '8px';
          wrapper.style.marginTop = '12px';

          // Move submit button into wrapper
          submitBtn.style.marginTop = '0'; // Remove margin since wrapper has it
          submitBtn.parentNode.insertBefore(wrapper, submitBtn);
          wrapper.appendChild(submitBtn);

          // Create and add cancel button
          const cancelBtn = document.createElement('button');
          cancelBtn.className = 'btn btn-secondary btn-cancel-edit';
          cancelBtn.textContent = 'Cancel';
          cancelBtn.onclick = cancelEditStep;
          wrapper.appendChild(cancelBtn);
        }
      }

      // Show the add-step panel
      const panel = document.getElementById('add-step-panel');
      panel.style.display = 'block';

      showToast('Editing step ' + (index + 1), 'info');
    }

    function cancelEditStep() {
      editingStepIndex = -1;

      // Reset button text back to "Add Step" (only main submit buttons, not branch editor buttons)
      document.querySelectorAll('.step-submit-btn .btn-text, .step-form > .btn-success .btn-text').forEach(btn => {
        btn.textContent = 'Add Step';
      });

      // Remove wrapper and restore submit button position
      document.querySelectorAll('.btn-edit-wrapper').forEach(wrapper => {
        const submitBtn = wrapper.querySelector('.step-submit-btn, .btn-success');
        if (submitBtn) {
          submitBtn.style.marginTop = '12px'; // Restore original margin
          wrapper.parentNode.insertBefore(submitBtn, wrapper);
        }
        wrapper.remove();
      });

      // Hide the panel
      addStepPanel();

      showToast('Edit canceled', 'info');
    }

    function deleteStep(index) {
      testSteps.splice(index, 1);
      renderSteps();
      // Note: renderSteps() already calls updateCode()
    }

    function clearAllSteps() {
      if (testSteps.length > 0 && !confirm('Clear all steps?')) return;
      testSteps = [];
      renderSteps();
      // Note: renderSteps() already calls updateCode()
    }

    // Drag and drop functionality
    let draggedIndex = -1;

    function handleDragStart(event) {
      draggedIndex = parseInt(event.target.closest('.step-item').dataset.index);
      event.target.closest('.step-item').style.opacity = '0.4';
      event.dataTransfer.effectAllowed = 'move';
    }

    function handleDragOver(event) {
      if (event.preventDefault) {
        event.preventDefault();
      }
      event.dataTransfer.dropEffect = 'move';
      return false;
    }

    function handleDrop(event) {
      if (event.stopPropagation) {
        event.stopPropagation();
      }

      const dropIndex = parseInt(event.target.closest('.step-item').dataset.index);

      if (draggedIndex !== dropIndex) {
        // Remove dragged item
        const draggedItem = testSteps[draggedIndex];
        testSteps.splice(draggedIndex, 1);

        // Insert at new position
        testSteps.splice(dropIndex, 0, draggedItem);

        renderSteps();
        // Note: renderSteps() already calls updateCode()
        showToast('Step moved successfully', 'success');
      }

      return false;
    }

    function handleDragEnd(event) {
      event.target.closest('.step-item').style.opacity = '1';
    }

    function updateCode() {
      try {
        // Update JSON
        const jsonCode = {
          name: "Custom Test",
          description: "Test created with Developer Playground",
          steps: testSteps
        };

        const jsonElement = document.getElementById('json-code');
        if (jsonElement) {
          const newJson = JSON.stringify(jsonCode, null, 2);

          // EXACT same pattern as stepsList update
          // Need to escape for HTML but preserve formatting
          const escapedJson = newJson
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');

          jsonElement.innerHTML = escapedJson;
        } else {
          showToast('ERROR: JSON element not found!', 'error');
        }

        // Update JavaScript
        let jsCode = `// Test: Custom Test\n// Generated by Developer Playground\n\n`;
        jsCode += `const { Browser } = require('@olib-ai/owl-browser-sdk');\n\n`;
        jsCode += `async function runTest() {\n`;
        jsCode += `  const browser = new Browser();\n`;
        jsCode += `  await browser.launch();\n\n`;
        jsCode += `  const page = await browser.newPage();\n\n`;

        testSteps.forEach((step, index) => {
          jsCode += `  // Step ${index + 1}: ${step.type}\n`;
          switch(step.type) {
            case 'navigate':
              jsCode += `  await page.goto('${step.url}');\n`;
              break;
            case 'click':
              jsCode += `  await page.click('${step.selector}');\n`;
              break;
            case 'type':
              jsCode += `  await page.type('${step.selector}', '${step.text}');\n`;
              break;
            case 'pick':
              jsCode += `  await page.pick('${step.selector}', '${step.value}');\n`;
              break;
            case 'wait':
              jsCode += `  await page.wait(${step.duration});\n`;
              break;
            case 'screenshot':
              if (step.mode === 'element') {
                jsCode += `  await page.screenshot({ path: '${step.filename}', mode: 'element', selector: '${step.selector}' });\n`;
              } else if (step.mode === 'fullpage') {
                jsCode += `  await page.screenshot({ path: '${step.filename}', mode: 'fullpage' });\n`;
              } else {
                jsCode += `  await page.screenshot({ path: '${step.filename}' });\n`;
              }
              break;
            case 'extract':
              jsCode += `  const text = await page.extractText('${step.selector}');\n`;
              jsCode += `  console.log(text);\n`;
              break;
            case 'submit_form':
              jsCode += `  await page.submitForm();\n`;
              break;
            case 'query':
              jsCode += `  const result = await page.queryPage('${step.query}');\n`;
              jsCode += `  console.log(result);\n`;
              break;
            case 'nla':
              jsCode += `  await page.executeNLA('${step.command}');\n`;
              break;
            case 'solve_captcha':
              if (step.provider && step.provider !== 'auto' || step.maxAttempts && step.maxAttempts !== 3) {
                const opts = [];
                if (step.maxAttempts && step.maxAttempts !== 3) {
                  opts.push(`maxAttempts: ${step.maxAttempts}`);
                }
                if (step.provider && step.provider !== 'auto') {
                  opts.push(`provider: '${step.provider}'`);
                }
                jsCode += `  await page.solveCaptcha({ ${opts.join(', ')} });\n`;
              } else {
                jsCode += `  await page.solveCaptcha();\n`;
              }
              break;
            case 'highlight':
              jsCode += `  await page.highlight('${step.selector}');\n`;
              break;
            case 'scroll_up':
              jsCode += `  await page.scrollToTop();\n`;
              break;
            case 'scroll_down':
              jsCode += `  await page.scrollToBottom();\n`;
              break;
            case 'record_video':
              jsCode += `  await page.startVideoRecording({ fps: ${step.fps || 30} });\n`;
              break;
            case 'stop_video':
              jsCode += `  const videoPath = await page.stopVideoRecording();\n`;
              jsCode += `  console.log('Video saved:', videoPath);\n`;
              break;
            case 'reload':
              if (step.ignoreCache) {
                jsCode += `  await page.reload({ ignoreCache: true });\n`;
              } else {
                jsCode += `  await page.reload();\n`;
              }
              break;
            case 'go_back':
              jsCode += `  await page.goBack();\n`;
              break;
            case 'go_forward':
              jsCode += `  await page.goForward();\n`;
              break;
            case 'press_key':
              jsCode += `  await page.pressKey('${step.key}');\n`;
              break;
            case 'drag_drop':
              if (step.midPoints && step.midPoints.length > 0) {
                jsCode += `  await page.dragDrop(${step.startX}, ${step.startY}, ${step.endX}, ${step.endY}, [\n`;
                step.midPoints.forEach((pt, idx) => {
                  jsCode += `    [${pt[0]}, ${pt[1]}]${idx < step.midPoints.length - 1 ? ',' : ''}\n`;
                });
                jsCode += `  ]);\n`;
              } else {
                jsCode += `  await page.dragDrop(${step.startX}, ${step.startY}, ${step.endX}, ${step.endY});\n`;
              }
              break;
            case 'html5_drag_drop':
              jsCode += `  await page.html5DragDrop('${step.sourceSelector}', '${step.targetSelector}');\n`;
              break;
            case 'mouse_move':
              if (step.steps > 0) {
                jsCode += `  await page.mouseMove(${step.startX}, ${step.startY}, ${step.endX}, ${step.endY}, ${step.steps});\n`;
              } else {
                jsCode += `  await page.mouseMove(${step.startX}, ${step.startY}, ${step.endX}, ${step.endY});\n`;
              }
              break;
            case 'hover':
              if (step.duration) {
                jsCode += `  await page.hover('${step.selector}', ${step.duration});\n`;
              } else {
                jsCode += `  await page.hover('${step.selector}');\n`;
              }
              break;
            case 'double_click':
              jsCode += `  await page.doubleClick('${step.selector}');\n`;
              break;
            case 'right_click':
              jsCode += `  await page.rightClick('${step.selector}');\n`;
              break;
            case 'clear_input':
              jsCode += `  await page.clearInput('${step.selector}');\n`;
              break;
            case 'focus':
              jsCode += `  await page.focus('${step.selector}');\n`;
              break;
            case 'blur':
              jsCode += `  await page.blur('${step.selector}');\n`;
              break;
            case 'select_all':
              jsCode += `  await page.selectAll('${step.selector}');\n`;
              break;
            case 'keyboard_combo':
              jsCode += `  await page.keyboardCombo('${step.key}', [${(step.modifiers || []).map(m => `'${m}'`).join(', ')}]);\n`;
              break;
            case 'upload_file':
              jsCode += `  await page.uploadFile('${step.selector}', [${(step.filePaths || []).map(p => `'${p}'`).join(', ')}]);\n`;
              break;
            case 'is_visible':
              jsCode += `  const isVisible = await page.isVisible('${step.selector}');\n`;
              jsCode += `  console.log('Is visible:', isVisible);\n`;
              break;
            case 'is_enabled':
              jsCode += `  const isEnabled = await page.isEnabled('${step.selector}');\n`;
              jsCode += `  console.log('Is enabled:', isEnabled);\n`;
              break;
            case 'is_checked':
              jsCode += `  const isChecked = await page.isChecked('${step.selector}');\n`;
              jsCode += `  console.log('Is checked:', isChecked);\n`;
              break;
            case 'get_attribute':
              jsCode += `  const attrValue = await page.getAttribute('${step.selector}', '${step.attribute}');\n`;
              jsCode += `  console.log('Attribute ${step.attribute}:', attrValue);\n`;
              break;
            case 'get_bounding_box':
              jsCode += `  const boundingBox = await page.getBoundingBox('${step.selector}');\n`;
              jsCode += `  console.log('Bounding box:', boundingBox);\n`;
              break;
            case 'evaluate':
              jsCode += `  const evalResult = await page.evaluate(\`${step.script.replace(/`/g, '\\`')}\`, { returnValue: ${step.return_value || false} });\n`;
              jsCode += `  console.log('Evaluate result:', evalResult);\n`;
              break;
            case 'clipboard_read':
              jsCode += `  const clipboardText = await page.clipboardRead();\n`;
              jsCode += `  console.log('Clipboard content:', clipboardText);\n`;
              break;
            case 'clipboard_write':
              jsCode += `  await page.clipboardWrite('${step.text.replace(/'/g, "\\'")}');\n`;
              jsCode += `  console.log('Text written to clipboard');\n`;
              break;
            case 'clipboard_clear':
              jsCode += `  await page.clipboardClear();\n`;
              jsCode += `  console.log('Clipboard cleared');\n`;
              break;
            case 'list_frames':
              jsCode += `  const frames = await page.listFrames();\n`;
              jsCode += `  console.log('Frames:', JSON.stringify(frames, null, 2));\n`;
              break;
            case 'switch_to_frame':
              jsCode += `  await page.switchToFrame('${step.frameSelector}');\n`;
              break;
            case 'switch_to_main_frame':
              jsCode += `  await page.switchToMainFrame();\n`;
              break;
            case 'scroll_by':
              jsCode += `  await page.scrollBy(${step.x || 0}, ${step.y || 500});\n`;
              break;
            case 'scroll_to_element':
              jsCode += `  await page.scrollToElement('${step.selector}');\n`;
              break;
            case 'wait_for_selector':
              jsCode += `  await page.waitForSelector('${step.selector}', { timeout: ${step.timeout || 5000} });\n`;
              break;
            case 'wait_for_network_idle':
              jsCode += `  await page.waitForNetworkIdle({ idleTime: ${step.idleTime || 500}, timeout: ${step.timeout || 30000} });\n`;
              break;
            case 'wait_for_function':
              jsCode += `  await page.waitForFunction('${step.function.replace(/'/g, "\\'")}', { timeout: ${step.timeout || 30000}, polling: ${step.polling || 100} });\n`;
              break;
            case 'wait_for_url':
              jsCode += `  await page.waitForUrl('${step.pattern}', { matchType: '${step.matchType || 'substring'}', timeout: ${step.timeout || 30000} });\n`;
              break;
            case 'get_html':
              jsCode += `  const html = await page.getHtml({ cleanLevel: '${step.cleanLevel || 'basic'}' });\n`;
              jsCode += `  console.log(html);\n`;
              break;
            case 'get_markdown':
              jsCode += `  const markdown = await page.getMarkdown({ includeImages: ${step.includeImages !== false}, includeLinks: ${step.includeLinks !== false} });\n`;
              jsCode += `  console.log(markdown);\n`;
              break;
            case 'extract_json':
              if (step.template) {
                jsCode += `  const json = await page.extractJson({ template: '${step.template}' });\n`;
              } else {
                jsCode += `  const json = await page.extractJson();\n`;
              }
              jsCode += `  console.log(JSON.stringify(json, null, 2));\n`;
              break;
            case 'get_page_info':
              jsCode += `  const pageInfo = await page.getPageInfo();\n`;
              jsCode += `  console.log(pageInfo);\n`;
              break;
            case 'summarize_page':
              jsCode += `  const summary = await page.summarizePage();\n`;
              jsCode += `  console.log(summary);\n`;
              break;
            case 'detect_captcha':
              jsCode += `  const captchaDetected = await page.detectCaptcha();\n`;
              jsCode += `  console.log(captchaDetected);\n`;
              break;
            case 'classify_captcha':
              jsCode += `  const captchaType = await page.classifyCaptcha();\n`;
              jsCode += `  console.log(captchaType);\n`;
              break;
            case 'get_cookies':
              if (step.url) {
                jsCode += `  const cookies = await page.getCookies('${step.url}');\n`;
              } else {
                jsCode += `  const cookies = await page.getCookies();\n`;
              }
              jsCode += `  console.log(JSON.stringify(cookies, null, 2));\n`;
              break;
            case 'set_cookie':
              jsCode += `  await page.setCookie({\n`;
              jsCode += `    url: '${step.url}',\n`;
              jsCode += `    name: '${step.name}',\n`;
              jsCode += `    value: '${step.value}'`;
              if (step.domain) {
                jsCode += `,\n    domain: '${step.domain}'`;
              }
              jsCode += `\n  });\n`;
              break;
            case 'delete_cookies':
              if (step.url && step.cookieName) {
                jsCode += `  await page.deleteCookies({ url: '${step.url}', cookieName: '${step.cookieName}' });\n`;
              } else if (step.url) {
                jsCode += `  await page.deleteCookies({ url: '${step.url}' });\n`;
              } else {
                jsCode += `  await page.deleteCookies();\n`;
              }
              break;
            case 'set_viewport':
              jsCode += `  await page.setViewport(${step.width || 1280}, ${step.height || 720});\n`;
              break;

            // ========== NETWORK INTERCEPTION ==========
            case 'add_network_rule':
              jsCode += `  const ruleId = await page.addNetworkRule({\n`;
              jsCode += `    urlPattern: '${step.urlPattern}',\n`;
              jsCode += `    action: '${step.action}'`;
              if (step.isRegex) jsCode += `,\n    isRegex: true`;
              if (step.action === 'redirect' && step.redirectUrl) {
                jsCode += `,\n    redirectUrl: '${step.redirectUrl}'`;
              }
              if (step.action === 'mock') {
                if (step.mockBody) jsCode += `,\n    mockBody: '${step.mockBody.replace(/'/g, "\\'")}'`;
                jsCode += `,\n    mockStatus: ${step.mockStatus || 200}`;
                if (step.mockContentType) jsCode += `,\n    mockContentType: '${step.mockContentType}'`;
              }
              jsCode += `\n  });\n`;
              jsCode += `  console.log('Rule created:', ruleId);\n`;
              break;
            case 'remove_network_rule':
              jsCode += `  await page.removeNetworkRule('${step.ruleId}');\n`;
              break;
            case 'enable_network_interception':
              jsCode += `  await page.setNetworkInterception(${step.enabled});\n`;
              break;
            case 'get_network_log':
              if (step.limit) {
                jsCode += `  const networkLog = await page.getNetworkLog(${step.limit});\n`;
              } else {
                jsCode += `  const networkLog = await page.getNetworkLog();\n`;
              }
              jsCode += `  console.log(JSON.stringify(networkLog, null, 2));\n`;
              break;
            case 'clear_network_log':
              jsCode += `  await page.clearNetworkLog();\n`;
              break;

            // ========== FILE DOWNLOADS ==========
            case 'set_download_path':
              jsCode += `  await page.setDownloadPath('${step.path}');\n`;
              break;
            case 'get_downloads':
              jsCode += `  const downloads = await page.getDownloads();\n`;
              jsCode += `  console.log(JSON.stringify(downloads, null, 2));\n`;
              break;
            case 'wait_for_download':
              if (step.downloadId) {
                jsCode += `  const download = await page.waitForDownload('${step.downloadId}', ${step.timeout || 30000});\n`;
              } else {
                jsCode += `  const download = await page.waitForDownload(null, ${step.timeout || 30000});\n`;
              }
              jsCode += `  console.log('Download completed:', download);\n`;
              break;
            case 'cancel_download':
              jsCode += `  await page.cancelDownload('${step.downloadId}');\n`;
              break;

            // ========== DIALOG HANDLING ==========
            case 'set_dialog_action':
              if (step.action === 'accept_with_text' && step.promptText) {
                jsCode += `  await page.setDialogAction('${step.dialogType}', '${step.action}', '${step.promptText}');\n`;
              } else {
                jsCode += `  await page.setDialogAction('${step.dialogType}', '${step.action}');\n`;
              }
              break;
            case 'get_pending_dialog':
              jsCode += `  const pendingDialog = await page.getPendingDialog();\n`;
              jsCode += `  console.log(pendingDialog);\n`;
              break;
            case 'handle_dialog':
              if (step.responseText) {
                jsCode += `  await page.handleDialog('${step.dialogId}', ${step.accept}, '${step.responseText}');\n`;
              } else {
                jsCode += `  await page.handleDialog('${step.dialogId}', ${step.accept});\n`;
              }
              break;
            case 'wait_for_dialog':
              jsCode += `  const dialog = await page.waitForDialog(${step.timeout || 5000});\n`;
              jsCode += `  console.log(dialog);\n`;
              break;

            // ========== TAB MANAGEMENT ==========
            case 'new_tab':
              if (step.url) {
                jsCode += `  const newTab = await page.newTab('${step.url}');\n`;
              } else {
                jsCode += `  const newTab = await page.newTab();\n`;
              }
              jsCode += `  console.log('New tab:', newTab);\n`;
              break;
            case 'get_tabs':
              jsCode += `  const tabs = await page.getTabs();\n`;
              jsCode += `  console.log(JSON.stringify(tabs, null, 2));\n`;
              break;
            case 'switch_tab':
              jsCode += `  await page.switchTab('${step.tabId}');\n`;
              break;
            case 'get_active_tab':
              jsCode += `  const activeTab = await page.getActiveTab();\n`;
              jsCode += `  console.log(activeTab);\n`;
              break;
            case 'close_tab':
              jsCode += `  await page.closeTab('${step.tabId}');\n`;
              break;
            case 'get_tab_count':
              jsCode += `  const tabCount = await page.getTabCount();\n`;
              jsCode += `  console.log('Tab count:', tabCount);\n`;
              break;
            case 'set_popup_policy':
              jsCode += `  await page.setPopupPolicy('${step.policy}');\n`;
              break;
            case 'get_blocked_popups':
              jsCode += `  const blockedPopups = await page.getBlockedPopups();\n`;
              jsCode += `  console.log(blockedPopups);\n`;
              break;

            // Control Flow
            case 'condition':
              jsCode += `  // Condition: ${formatCondition(step.condition)}\n`;
              jsCode += `  // Note: Condition execution requires flow runner\n`;
              if (step.onTrue && step.onTrue.length > 0) {
                jsCode += `  // Then branch: ${step.onTrue.length} step(s)\n`;
              }
              if (step.onFalse && step.onFalse.length > 0) {
                jsCode += `  // Else branch: ${step.onFalse.length} step(s)\n`;
              }
              break;
          }
          jsCode += `\n`;
        });

        jsCode += `  await browser.close();\n`;
        jsCode += `}\n\n`;
        jsCode += `runTest().catch(console.error);`;

        const jsElement = document.getElementById('javascript-code');
        if (jsElement) {
          // EXACT same pattern as stepsList update
          const escapedJs = jsCode
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');

          jsElement.innerHTML = escapedJs;
        }
      } catch (error) {
        showToast('ERROR updating code: ' + error.message, 'error');
      }
    }

    function switchTab(tab) {
      document.querySelectorAll('.tab-btn').forEach(btn => {
        // Use data attribute for matching instead of textContent (which includes child spans)
        const btnTab = btn.getAttribute('data-tab');
        if (btnTab === tab) {
          btn.classList.add('active');
        } else {
          btn.classList.remove('active');
        }
      });

      document.querySelectorAll('.tab-content').forEach(content => {
        content.classList.remove('active');
      });

      document.getElementById(tab + '-tab').classList.add('active');
    }

    function exportTest() {
      const jsonCode = {
        name: "Custom Test",
        description: "Test created with Developer Playground",
        steps: testSteps
      };

      const blob = new Blob([JSON.stringify(jsonCode, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'test.json';
      a.click();
      URL.revokeObjectURL(url);
    }

    function handleImport(event) {
      const file = event.target.files[0];
      if (!file) return;

      const reader = new FileReader();
      reader.onload = function(e) {
        try {
          const json = JSON.parse(e.target.result);

          // Clear existing steps
          testSteps = [];

          // Import steps from JSON
          if (json.steps && Array.isArray(json.steps)) {
            testSteps = json.steps;

            // Update the UI
            renderSteps();
            updateCode();

            // Show success message
            showToast('Test imported successfully! ' + testSteps.length + ' steps loaded.', 'success');
          } else {
            showToast('Invalid JSON format. Expected format:\n{\n  "name": "Test Name",\n  "steps": [...]\n}', 'error');
          }
        } catch (error) {
          showToast('Error parsing JSON: ' + error.message, 'error');
        }

        // Reset file input
        event.target.value = '';
      };
      reader.readAsText(file);
    }

    function executeTest() {
      if (testSteps.length === 0) {
        showToast('No steps to execute!', 'error');
        return;
      }

      // Filter only selected steps
      const selectedSteps = testSteps.filter(step => step.selected !== false);

      if (selectedSteps.length === 0) {
        showToast('No steps selected! Please select at least one step to execute.', 'error');
        return;
      }

      // Update status
      document.getElementById('status-badge').textContent = 'RUNNING';
      document.getElementById('status-badge').className = 'status-badge status-running';

      // Send only selected steps to C++ for execution
      if (typeof _2 !== 'undefined') {
        _2('execute_test', JSON.stringify(selectedSteps));
        if (selectedSteps.length < testSteps.length) {
          showToast(`Executing ${selectedSteps.length} of ${testSteps.length} steps`, 'info');
        }
      } else {
        updateExecutionStatus('error', 'Message sending not available');
      }
    }

    function updateExecutionStatus(status, message, currentStep, totalSteps) {
      const badge = document.getElementById('status-badge');
      const info = document.querySelector('.status-info');

      switch(status) {
        case 'running':
          badge.textContent = 'RUNNING';
          badge.className = 'status-badge status-running';
          if (currentStep !== undefined && totalSteps !== undefined) {
            info.textContent = `Step ${currentStep}/${totalSteps} · ${message}`;
          } else {
            info.textContent = message || 'Executing test...';
          }
          break;
        case 'success':
          badge.textContent = 'SUCCESS';
          badge.className = 'status-badge status-success';
          info.textContent = message || 'Test completed successfully';
          setTimeout(() => {
            badge.textContent = 'IDLE';
            badge.className = 'status-badge status-idle';
            info.textContent = `${testSteps.length} step${testSteps.length !== 1 ? 's' : ''} · Ready to execute`;
          }, 3000);
          break;
        case 'error':
          badge.textContent = 'ERROR';
          badge.className = 'status-badge status-error';
          info.textContent = message || 'Test failed';
          setTimeout(() => {
            badge.textContent = 'IDLE';
            badge.className = 'status-badge status-idle';
            info.textContent = `${testSteps.length} step${testSteps.length !== 1 ? 's' : ''} · Ready to execute`;
          }, 5000);
          break;
        case 'idle':
          badge.textContent = 'IDLE';
          badge.className = 'status-badge status-idle';
          info.textContent = `${testSteps.length} step${testSteps.length !== 1 ? 's' : ''} · Ready to execute`;
          break;
      }
    }

    window.updateExecutionStatus = updateExecutionStatus;

    // Element Picker functionality
    let currentPickerTarget = null;

    function toggleScreenshotSelector() {
      const mode = document.getElementById('screenshot-mode').value;
      const selectorGroup = document.getElementById('screenshot-selector-group');
      if (mode === 'element') {
        selectorGroup.style.display = 'block';
      } else {
        selectorGroup.style.display = 'none';
      }
    }

    function toggleConditionSourceStep() {
      const source = document.getElementById('condition-source').value;
      const stepGroup = document.getElementById('condition-source-step-group');
      stepGroup.style.display = source === 'step' ? 'block' : 'none';
    }

    function toggleConditionValue() {
      const operator = document.getElementById('condition-operator').value;
      const valueGroup = document.getElementById('condition-value-group');
      // Operators that don't need a value
      const noValueOperators = ['is_truthy', 'is_falsy', 'is_empty', 'is_not_empty'];
      valueGroup.style.display = noValueOperators.includes(operator) ? 'none' : 'block';
    }

    // Format condition for display
    function formatCondition(condition) {
      if (!condition) return '';
      const operatorLabels = {
        'equals': '==',
        'not_equals': '!=',
        'contains': 'contains',
        'not_contains': 'not contains',
        'starts_with': 'starts with',
        'ends_with': 'ends with',
        'greater_than': '>',
        'less_than': '<',
        'is_truthy': 'is truthy',
        'is_falsy': 'is falsy',
        'is_empty': 'is empty',
        'is_not_empty': 'is not empty',
        'regex_match': 'matches regex'
      };
      const source = condition.source === 'step' ? `Step ${condition.sourceStepId || condition.sourceStepIndex}` : 'Previous';
      const field = condition.field ? `.${condition.field}` : '';
      const op = operatorLabels[condition.operator] || condition.operator;
      const noValueOps = ['is_truthy', 'is_falsy', 'is_empty', 'is_not_empty'];
      if (noValueOps.includes(condition.operator)) {
        return `${source}${field} ${op}`;
      }
      return `${source}${field} ${op} "${condition.value}"`;
    }

    // Format a branch step for display
    function formatBranchStep(step) {
      if (!step) return '';
      switch(step.type) {
        case 'navigate': return step.url || '';
        case 'click': return step.selector || '';
        case 'type': return `"${step.text}" → ${step.selector}`;
        case 'wait': return `${step.duration}ms`;
        case 'extract': return step.selector || 'body';
        case 'screenshot': return step.filename || step.mode || '';
        case 'query': return step.query || '';
        case 'condition': return formatCondition(step.condition);
        default: return step.selector || step.url || step.value || '';
      }
    }

    function startElementPicker(inputId) {
      currentPickerTarget = inputId;
      showToast('Click on an element in the main browser window', 'info');

      // Send message to C++ to start element picker mode
      if (typeof _2 !== 'undefined') {
        _2('start_element_picker', inputId);
      } else {
        showToast('Element picker not available', 'error');
      }
    }

    function onElementPicked(selector) {
      if (!currentPickerTarget) return;

      const input = document.getElementById(currentPickerTarget);
      if (input) {
        input.value = selector;
        input.focus();
        showToast('Element selector set: ' + selector, 'success');
      }
      currentPickerTarget = null;
    }

    function onElementPickerCanceled() {
      if (currentPickerTarget) {
        showToast('Element picker canceled', 'info');
      }
      currentPickerTarget = null;
    }

    // Position Picker Functions
    function startPositionPicker(inputId) {
      currentPickerTarget = inputId;
      showToast('Click on a position in the main browser window', 'info');

      // Send message to C++ to start position picker mode
      if (typeof _2 !== 'undefined') {
        _2('start_position_picker', inputId);
      } else {
        showToast('Position picker not available', 'error');
      }
    }

    function onPositionPicked(position) {
      if (!currentPickerTarget) return;

      const input = document.getElementById(currentPickerTarget);
      if (input) {
        input.value = position;
        input.focus();
        showToast('Position set: ' + position, 'success');
      }
      currentPickerTarget = null;
    }

    function onPositionPickerCanceled() {
      if (currentPickerTarget) {
        showToast('Position picker canceled', 'info');
      }
      currentPickerTarget = null;
    }

    // Add drag waypoint dynamically
    let waypointCounter = 0;
    function addDragWaypoint() {
      const container = document.getElementById('drag-waypoints-container');
      if (!container) return;

      waypointCounter++;
      const waypointId = 'drag-waypoint-' + waypointCounter;

      const waypointDiv = document.createElement('div');
      waypointDiv.className = 'waypoint-row';
      waypointDiv.style.cssText = 'display: flex; gap: 8px; margin-bottom: 8px; align-items: center;';
      waypointDiv.innerHTML = `
        <span style="font-size: 11px; color: #888; min-width: 50px;">Point ${waypointCounter}:</span>
        <div class="input-with-picker" style="flex: 1;">
          <input type="text" class="form-input drag-waypoint-input" id="${waypointId}" placeholder="200x300">
          <button class="btn-picker" onclick="startPositionPicker('${waypointId}')" title="Pick waypoint position from page">
            ${ICON_LOCATION_ARROW}
          </button>
        </div>
        <button class="btn btn-danger btn-icon btn-sm" onclick="removeDragWaypoint(this)" title="Remove waypoint" style="padding: 4px 8px;">
          ${ICON_TRASH}
        </button>
      `;

      container.appendChild(waypointDiv);
    }

    function removeDragWaypoint(btn) {
      const row = btn.closest('.waypoint-row');
      if (row) {
        row.remove();
        // Re-number the remaining waypoints
        const rows = document.querySelectorAll('.waypoint-row');
        rows.forEach((row, idx) => {
          const label = row.querySelector('span');
          if (label) {
            label.textContent = 'Point ' + (idx + 1) + ':';
          }
        });
      }
    }

    // Expose functions to window for C++ to call
    window.onElementPicked = onElementPicked;
    window.onElementPickerCanceled = onElementPickerCanceled;
    window.onPositionPicked = onPositionPicked;
    window.onPositionPickerCanceled = onPositionPickerCanceled;

    // TLD Autocomplete System
    const TOP_TLDS = [
      { tld: '.com', description: 'Commercial' },
      { tld: '.org', description: 'Organization' },
      { tld: '.net', description: 'Network' },
      { tld: '.io', description: 'Tech startups' },
      { tld: '.co', description: 'Company' },
      { tld: '.ai', description: 'Artificial Intelligence' },
      { tld: '.dev', description: 'Developers' },
      { tld: '.app', description: 'Applications' },
      { tld: '.tech', description: 'Technology' },
      { tld: '.me', description: 'Personal' },
      { tld: '.info', description: 'Information' },
      { tld: '.biz', description: 'Business' },
      { tld: '.ca', description: 'Canada' },
      { tld: '.uk', description: 'United Kingdom' },
      { tld: '.de', description: 'Germany' },
      { tld: '.fr', description: 'France' },
      { tld: '.jp', description: 'Japan' },
      { tld: '.cn', description: 'China' },
      { tld: '.in', description: 'India' },
      { tld: '.br', description: 'Brazil' },
      { tld: '.au', description: 'Australia' },
      { tld: '.ru', description: 'Russia' },
      { tld: '.ch', description: 'Switzerland' },
      { tld: '.it', description: 'Italy' },
      { tld: '.nl', description: 'Netherlands' },
      { tld: '.se', description: 'Sweden' },
      { tld: '.no', description: 'Norway' },
      { tld: '.es', description: 'Spain' },
      { tld: '.pl', description: 'Poland' }
    ];

    function setupTLDAutocomplete(inputId, dropdownId) {
      const input = document.getElementById(inputId);
      const dropdown = document.getElementById(dropdownId);
      if (!input || !dropdown) return;

      let currentSelection = -1;

      // Auto-add https:// if no protocol
      input.addEventListener('blur', () => {
        let value = input.value.trim();
        if (value && !value.match(/^[a-z]+:\/\//i)) {
          input.value = 'https://' + value;
        }
      });

      // Handle TLD autocomplete
      input.addEventListener('input', (e) => {
        const value = e.target.value;

        // Extract the part after the last dot
        const lastDotIndex = value.lastIndexOf('.');
        if (lastDotIndex === -1 || lastDotIndex === value.length - 1) {
          // If there's a dot at the end, show all top TLDs
          if (value.endsWith('.')) {
            showTLDSuggestions('', value.substring(0, lastDotIndex));
          } else {
            dropdown.classList.remove('show');
          }
          return;
        }

        // Get the search term after the last dot
        const afterDot = value.substring(lastDotIndex + 1);
        const beforeDot = value.substring(0, lastDotIndex);

        // Only show suggestions if we have a dot and some characters after it
        if (afterDot.length > 0) {
          showTLDSuggestions(afterDot, beforeDot);
        } else {
          dropdown.classList.remove('show');
        }
      });

      function showTLDSuggestions(searchTerm, domain) {
        // Filter TLDs based on search term (remove the leading dot from comparison)
        let filtered = TOP_TLDS.filter(item => {
          const tldWithoutDot = item.tld.substring(1); // Remove leading dot
          return searchTerm === '' || tldWithoutDot.startsWith(searchTerm.toLowerCase());
        });

        // Remove exact matches from suggestions (user has already typed complete TLD)
        // But only if there are other partial matches available
        const exactMatches = filtered.filter(item => {
          const tldWithoutDot = item.tld.substring(1);
          return tldWithoutDot.toLowerCase() === searchTerm.toLowerCase();
        });

        const partialMatches = filtered.filter(item => {
          const tldWithoutDot = item.tld.substring(1);
          return tldWithoutDot.toLowerCase() !== searchTerm.toLowerCase();
        });

        // If there's an exact match and no other partial matches, hide dropdown
        if (exactMatches.length > 0 && partialMatches.length === 0) {
          dropdown.classList.remove('show');
          return;
        }

        // Show only partial matches (exclude exact matches from dropdown)
        filtered = partialMatches.slice(0, 5); // Show max 5 suggestions

        if (filtered.length === 0) {
          dropdown.classList.remove('show');
          return;
        }

        // Build dropdown HTML
        dropdown.innerHTML = filtered.map((item, index) => `
          <div class="tld-autocomplete-item" data-index="${index}" data-tld="${item.tld}">
            <span class="tld-domain">${domain}${item.tld}</span>
            <span class="tld-description">${item.description}</span>
          </div>
        `).join('');

        // Add click handlers
        dropdown.querySelectorAll('.tld-autocomplete-item').forEach((item, index) => {
          item.addEventListener('mouseenter', () => {
            currentSelection = index;
            updateSelection();
          });
          item.addEventListener('click', () => {
            selectTLD(item.dataset.tld, domain);
          });
        });

        dropdown.classList.add('show');
        currentSelection = -1;
      }

      function selectTLD(tld, domain) {
        const fullUrl = domain + tld;
        input.value = fullUrl;
        dropdown.classList.remove('show');
        currentSelection = -1;

        // Auto-add navigate step when TLD is selected
        addStep('navigate');
      }

      function updateSelection() {
        const items = dropdown.querySelectorAll('.tld-autocomplete-item');
        items.forEach((item, index) => {
          if (index === currentSelection) {
            item.classList.add('active');
          } else {
            item.classList.remove('active');
          }
        });
      }

      // Keyboard navigation
      input.addEventListener('keydown', (e) => {
        if (!dropdown.classList.contains('show')) return;

        const items = dropdown.querySelectorAll('.tld-autocomplete-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') {
          e.preventDefault();
          currentSelection = Math.min(currentSelection + 1, items.length - 1);
          updateSelection();
        } else if (e.key === 'ArrowUp') {
          e.preventDefault();
          currentSelection = Math.max(currentSelection - 1, -1);
          updateSelection();
        } else if (e.key === 'Enter' && currentSelection >= 0) {
          e.preventDefault();
          const selectedItem = items[currentSelection];
          if (selectedItem) {
            const tld = selectedItem.dataset.tld;
            const value = input.value;
            const lastDotIndex = value.lastIndexOf('.');
            const domain = value.substring(0, lastDotIndex);
            selectTLD(tld, domain);
          }
        } else if (e.key === 'Escape') {
          dropdown.classList.remove('show');
          currentSelection = -1;
        }
      });

      // Close dropdown when clicking outside
      document.addEventListener('click', (e) => {
        if (!input.contains(e.target) && !dropdown.contains(e.target)) {
          dropdown.classList.remove('show');
          currentSelection = -1;
        }
      });
    }

    // Initialize TLD autocomplete for navigate URL input
    setupTLDAutocomplete('navigate-url', 'navigate-tld-dropdown');

    // owl:// Schema Autocomplete System
    // Available pages (dynamically updated)
    const OLIB_PAGES = [
      { page: 'homepage.html', description: 'Browser Homepage' },
      { page: 'signin_form.html', description: 'Sign In Form Test Page' },
      { page: 'user_form.html', description: 'User Form Test Page' }
    ];

    function setupOlibAutocomplete(inputId, dropdownId) {
      const input = document.getElementById(inputId);
      const dropdown = document.getElementById(dropdownId);
      if (!input || !dropdown) return;

      let currentSelection = -1;

      input.addEventListener('input', (e) => {
        const value = e.target.value;

        // Check if value starts with owl://
        if (value.startsWith('owl://')) {
          const afterSchema = value.substring(7); // After 'owl://'

          // Filter pages based on what's typed after owl://
          const filtered = OLIB_PAGES.filter(item => {
            return afterSchema === '' || item.page.toLowerCase().startsWith(afterSchema.toLowerCase());
          });

          // Remove exact matches from suggestions
          const exactMatches = filtered.filter(item => {
            return item.page.toLowerCase() === afterSchema.toLowerCase();
          });

          const partialMatches = filtered.filter(item => {
            return item.page.toLowerCase() !== afterSchema.toLowerCase();
          });

          // If exact match and no partials, hide dropdown
          if (exactMatches.length > 0 && partialMatches.length === 0) {
            dropdown.classList.remove('show');
            return;
          }

          if (partialMatches.length === 0) {
            dropdown.classList.remove('show');
            return;
          }

          // Build dropdown HTML
          dropdown.innerHTML = partialMatches.map((item, index) => `
            <div class="tld-autocomplete-item" data-index="${index}" data-page="${item.page}">
              <span class="tld-domain">owl://${item.page}</span>
              <span class="tld-description">${item.description}</span>
            </div>
          `).join('');

          // Add click handlers
          dropdown.querySelectorAll('.tld-autocomplete-item').forEach((item, index) => {
            item.addEventListener('mouseenter', () => {
              currentSelection = index;
              updateOlibSelection();
            });
            item.addEventListener('click', () => {
              selectOlibPage(item.dataset.page);
            });
          });

          dropdown.classList.add('show');
          currentSelection = -1;
        } else {
          dropdown.classList.remove('show');
        }
      });

      function selectOlibPage(page) {
        const fullUrl = 'owl://' + page;
        input.value = fullUrl;
        dropdown.classList.remove('show');
        currentSelection = -1;

        // Auto-add navigate step when olib page is selected
        addStep('navigate');
      }

      function updateOlibSelection() {
        const items = dropdown.querySelectorAll('.tld-autocomplete-item');
        items.forEach((item, index) => {
          if (index === currentSelection) {
            item.classList.add('active');
          } else {
            item.classList.remove('active');
          }
        });
      }

      // Keyboard navigation
      input.addEventListener('keydown', (e) => {
        if (!dropdown.classList.contains('show')) return;

        const items = dropdown.querySelectorAll('.tld-autocomplete-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') {
          e.preventDefault();
          currentSelection = Math.min(currentSelection + 1, items.length - 1);
          updateOlibSelection();
        } else if (e.key === 'ArrowUp') {
          e.preventDefault();
          currentSelection = Math.max(currentSelection - 1, -1);
          updateOlibSelection();
        } else if (e.key === 'Enter' && currentSelection >= 0) {
          e.preventDefault();
          const selectedItem = items[currentSelection];
          if (selectedItem) {
            selectOlibPage(selectedItem.dataset.page);
          }
        } else if (e.key === 'Escape') {
          dropdown.classList.remove('show');
          currentSelection = -1;
        }
      });

      // Close dropdown when clicking outside
      document.addEventListener('click', (e) => {
        if (!input.contains(e.target) && !dropdown.contains(e.target)) {
          dropdown.classList.remove('show');
          currentSelection = -1;
        }
      });
    }

    // Initialize owl:// autocomplete (reuse same dropdown as TLD)
    setupOlibAutocomplete('navigate-url', 'navigate-tld-dropdown');

    // ===== OUTPUT TAB FUNCTIONALITY =====
    let outputs = [];

    function addOutput(stepNum, type, data, timestamp) {
      const output = {
        id: Date.now(),
        step: stepNum,
        type: type,
        data: data,
        timestamp: timestamp || new Date().toLocaleTimeString()
      };
      outputs.push(output);
      renderOutputs();
      updateOutputCount();

      // Auto-switch to output tab if this is the first output
      if (outputs.length === 1) {
        switchTab('output');
      }
    }

    function renderOutputs() {
      const container = document.getElementById('output-container');
      const clearBtn = document.getElementById('clear-outputs-btn');

      if (outputs.length === 0) {
        container.innerHTML = `
          <div class="empty-state">
            )HTML" + std::string(OlibIcons::CODE) + R"HTML(
            <div class="empty-state-text">No outputs yet. Run a test with screenshot, extract, or get operations.</div>
          </div>
        `;
        if (clearBtn) clearBtn.style.display = 'none';
        return;
      }

      if (clearBtn) clearBtn.style.display = 'inline-flex';

      container.innerHTML = outputs.map((output, index) => {
        let bodyContent = '';
        const typeLabel = output.type.replace(/_/g, ' ').toUpperCase();

        switch (output.type) {
          case 'screenshot':
            bodyContent = `<img class="output-image" src="data:image/png;base64,${output.data}" alt="Screenshot from step ${output.step}">`;
            break;
          case 'extract':
          case 'query':
          case 'summarize_page':
            bodyContent = `<pre class="output-text">${escapeHtml(output.data)}</pre>`;
            break;
          case 'get_html':
          case 'get_markdown':
            bodyContent = `<pre class="output-html">${escapeHtml(output.data)}</pre>`;
            break;
          case 'extract_json':
          case 'get_cookies':
          case 'get_page_info':
          case 'detect_captcha':
          case 'classify_captcha':
            bodyContent = `<pre class="output-json">${syntaxHighlightJson(output.data)}</pre>`;
            break;
          case 'video':
            bodyContent = `<div class="output-text">Video saved to: ${escapeHtml(output.data)}</div>`;
            break;
          default:
            bodyContent = `<pre class="output-text">${escapeHtml(typeof output.data === 'object' ? JSON.stringify(output.data, null, 2) : output.data)}</pre>`;
        }

        return `
          <div class="output-item" id="output-${output.id}">
            <div class="output-header" onclick="toggleOutputBody(${output.id})">
              <div class="output-title">
                <span class="output-step">Step ${output.step}</span>
                <span class="output-type">${typeLabel}</span>
              </div>
              <div style="display: flex; align-items: center; gap: 10px;">
                <span class="output-timestamp">${output.timestamp}</span>
                <div class="output-actions">
                  ${output.type === 'screenshot' ? `
                  <button class="btn btn-secondary btn-icon" onclick="event.stopPropagation(); downloadScreenshot(${index})" title="Download screenshot">
                    )HTML" + std::string(OlibIcons::ARROW_DOWN_TO_LINE) + R"HTML(
                  </button>
                  ` : ''}
                  <button class="btn btn-secondary btn-icon" onclick="event.stopPropagation(); copyOutput(${index})" title="Copy to clipboard">
                    )HTML" + std::string(OlibIcons::CLIPBOARD) + R"HTML(
                  </button>
                  <button class="btn btn-danger btn-icon" onclick="event.stopPropagation(); deleteOutput(${index})" title="Delete">
                    )HTML" + std::string(OlibIcons::TRASH) + R"HTML(
                  </button>
                </div>
              </div>
            </div>
            <div class="output-body" id="output-body-${output.id}">
              ${bodyContent}
            </div>
          </div>
        `;
      }).join('');
    }

    function escapeHtml(text) {
      if (typeof text !== 'string') text = String(text);
      const div = document.createElement('div');
      div.textContent = text;
      return div.innerHTML;
    }

    function syntaxHighlightJson(data) {
      let jsonStr;
      if (typeof data === 'string') {
        try {
          jsonStr = JSON.stringify(JSON.parse(data), null, 2);
        } catch (e) {
          jsonStr = data;
        }
      } else {
        jsonStr = JSON.stringify(data, null, 2);
      }

      return escapeHtml(jsonStr)
        .replace(/"([^"]+)":/g, '<span class="json-key">"$1"</span>:')
        .replace(/: "([^"]*)"([,\n])/g, ': <span class="json-string">"$1"</span>$2')
        .replace(/: (\d+\.?\d*)([,\n])/g, ': <span class="json-number">$1</span>$2')
        .replace(/: (true|false)([,\n])/g, ': <span class="json-boolean">$1</span>$2')
        .replace(/: (null)([,\n])/g, ': <span class="json-null">$1</span>$2');
    }

    function toggleOutputBody(outputId) {
      const body = document.getElementById('output-body-' + outputId);
      if (body) {
        body.classList.toggle('collapsed');
      }
    }

    function copyOutput(index) {
      const output = outputs[index];
      if (!output) {
        showToast('Output not found', 'error');
        return;
      }

      let textToCopy = '';

      if (output.type === 'screenshot') {
        textToCopy = 'data:image/png;base64,' + output.data;
      } else if (typeof output.data === 'object') {
        textToCopy = JSON.stringify(output.data, null, 2);
      } else if (output.data !== undefined && output.data !== null) {
        textToCopy = String(output.data);
      } else {
        showToast('No data to copy', 'error');
        return;
      }

      // Try modern clipboard API first, fallback to execCommand
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(textToCopy).then(() => {
          showToast('Copied to clipboard', 'success');
        }).catch(err => {
          // Fallback to execCommand
          copyToClipboardFallback(textToCopy);
        });
      } else {
        copyToClipboardFallback(textToCopy);
      }
    }

    function copyToClipboardFallback(text) {
      const textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.style.position = 'fixed';
      textarea.style.left = '-9999px';
      document.body.appendChild(textarea);
      textarea.select();
      try {
        document.execCommand('copy');
        showToast('Copied to clipboard', 'success');
      } catch (err) {
        showToast('Failed to copy: ' + err, 'error');
      }
      document.body.removeChild(textarea);
    }

    function downloadScreenshot(index) {
      const output = outputs[index];
      if (output.type !== 'screenshot') return;

      // Create a download link
      const link = document.createElement('a');
      link.href = 'data:image/png;base64,' + output.data;
      link.download = 'screenshot_step_' + output.step + '_' + Date.now() + '.png';
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      showToast('Screenshot downloaded', 'success');
    }

    function deleteOutput(index) {
      outputs.splice(index, 1);
      renderOutputs();
      updateOutputCount();
    }

    function clearOutputs() {
      if (outputs.length === 0) return;
      if (!confirm('Clear all outputs?')) return;
      outputs = [];
      renderOutputs();
      updateOutputCount();
    }

    function updateOutputCount() {
      const countEl = document.getElementById('output-count');
      if (countEl) {
        if (outputs.length > 0) {
          countEl.textContent = outputs.length;
          countEl.style.display = 'inline';
        } else {
          countEl.style.display = 'none';
        }
      }
    }

    // Receive output from C++ execution
    function receiveOutput(stepNum, type, data) {
      addOutput(stepNum, type, data);
    }

    // Expose to window for C++ to call
    window.receiveOutput = receiveOutput;
    window.addOutput = addOutput;
    window.clearOutputs = clearOutputs;

    // Initialize
    updateCode();

    // ========== BRANCH STEP MANAGEMENT ==========
    let currentBranchTarget = null;  // 'onTrue' or 'onFalse'
    let editingConditionBranches = { onTrue: [], onFalse: [] };

    function openBranchStepModal(branchType) {
      currentBranchTarget = branchType;
      document.getElementById('branch-step-modal').style.display = 'flex';
    }

    function closeBranchStepModal() {
      document.getElementById('branch-step-modal').style.display = 'none';
      currentBranchTarget = null;
    }

    function addBranchStep(stepType) {
      if (!currentBranchTarget) return;

      // Get parameters based on step type
      let step = { type: stepType, selected: true };
      const modal = document.getElementById('branch-step-modal');

      switch (stepType) {
        case 'navigate':
          step.url = modal.querySelector('#branch-navigate-url').value || 'https://example.com';
          break;
        case 'click':
          step.selector = modal.querySelector('#branch-click-selector').value || '';
          break;
        case 'type':
          step.selector = modal.querySelector('#branch-type-selector').value || '';
          step.text = modal.querySelector('#branch-type-text').value || '';
          break;
        case 'wait':
          step.duration = parseInt(modal.querySelector('#branch-wait-duration').value) || 1000;
          break;
        case 'screenshot':
          step.mode = 'viewport';
          break;
        case 'extract':
          step.selector = modal.querySelector('#branch-extract-selector').value || 'body';
          break;
        case 'scroll_up':
        case 'scroll_down':
          // No params needed
          break;
        case 'press_key':
          step.key = modal.querySelector('#branch-press-key').value || 'Enter';
          break;
        case 'highlight':
          step.selector = modal.querySelector('#branch-highlight-selector').value || '';
          break;
      }

      // Add to appropriate branch
      editingConditionBranches[currentBranchTarget].push(step);

      // Update the branch display
      renderBranchSteps();

      // Reset modal form and close
      resetBranchModalForm();
      closeBranchStepModal();
    }

    function removeBranchStep(branchType, index) {
      editingConditionBranches[branchType].splice(index, 1);
      renderBranchSteps();
    }

    function renderBranchSteps() {
      // Render Then branch
      const thenContainer = document.getElementById('condition-then-steps');
      if (editingConditionBranches.onTrue.length === 0) {
        thenContainer.innerHTML = '<p style="font-size: 11px; color: #666; margin: 0; font-style: italic;">No steps added</p>';
      } else {
        thenContainer.innerHTML = editingConditionBranches.onTrue.map((s, i) => `
          <div style="display: flex; justify-content: space-between; align-items: center; padding: 6px 8px; background: rgba(74, 222, 128, 0.1); border-radius: 4px; margin-bottom: 4px;">
            <span style="font-size: 12px; color: #e0e0e0;">${i + 1}. ${s.type}: ${formatBranchStep(s)}</span>
            <button type="button" onclick="removeBranchStep('onTrue', ${i})" style="background: none; border: none; color: #f87171; cursor: pointer; padding: 2px 6px;">&times;</button>
          </div>
        `).join('');
      }

      // Render Else branch
      const elseContainer = document.getElementById('condition-else-steps');
      if (editingConditionBranches.onFalse.length === 0) {
        elseContainer.innerHTML = '<p style="font-size: 11px; color: #666; margin: 0; font-style: italic;">No steps added</p>';
      } else {
        elseContainer.innerHTML = editingConditionBranches.onFalse.map((s, i) => `
          <div style="display: flex; justify-content: space-between; align-items: center; padding: 6px 8px; background: rgba(248, 113, 113, 0.1); border-radius: 4px; margin-bottom: 4px;">
            <span style="font-size: 12px; color: #e0e0e0;">${i + 1}. ${s.type}: ${formatBranchStep(s)}</span>
            <button type="button" onclick="removeBranchStep('onFalse', ${i})" style="background: none; border: none; color: #f87171; cursor: pointer; padding: 2px 6px;">&times;</button>
          </div>
        `).join('');
      }
    }

    function resetBranchModalForm() {
      const modal = document.getElementById('branch-step-modal');
      modal.querySelectorAll('input').forEach(input => input.value = '');
      modal.querySelectorAll('select').forEach(select => select.selectedIndex = 0);
      showBranchStepForm('navigate');
    }

    function showBranchStepForm(stepType) {
      const modal = document.getElementById('branch-step-modal');
      modal.querySelectorAll('.branch-step-form').forEach(form => form.style.display = 'none');
      const form = modal.querySelector(`.branch-step-form[data-type="${stepType}"]`);
      if (form) form.style.display = 'block';
    }

    // Update editStep to show branch editor for conditions
    const originalEditStep = editStep;
    editStep = function(index) {
      const step = testSteps[index];
      if (step && step.type === 'condition') {
        // Load existing branches
        editingConditionBranches = {
          onTrue: step.onTrue ? [...step.onTrue] : [],
          onFalse: step.onFalse ? [...step.onFalse] : []
        };
        // Show branch editor, hide info message
        document.getElementById('condition-branch-editor').style.display = 'block';
        document.getElementById('condition-info-message').style.display = 'none';
        // Render existing branch steps
        renderBranchSteps();
      }
      originalEditStep(index);
    };

    // Update addStep to include branches when saving condition
    const originalGetStepData = window.getStepData;
    window.getConditionBranches = function() {
      return editingConditionBranches;
    };

    // Reset branch editor when switching step types
    const originalSelectStepType = selectStepType;
    selectStepType = function(type) {
      if (type !== 'condition') {
        document.getElementById('condition-branch-editor').style.display = 'none';
        document.getElementById('condition-info-message').style.display = 'block';
        editingConditionBranches = { onTrue: [], onFalse: [] };
      }
      originalSelectStepType(type);
    };
  </script>

  <!-- Branch Step Modal -->
  <div id="branch-step-modal" style="display: none; position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.7); z-index: 1000; align-items: center; justify-content: center;">
    <div style="background: #1e1e2e; border-radius: 12px; padding: 20px; width: 400px; max-height: 80vh; overflow-y: auto;">
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
        <h3 style="margin: 0; color: #e0e0e0;">Add Branch Step</h3>
        <button onclick="closeBranchStepModal()" style="background: none; border: none; color: #888; font-size: 20px; cursor: pointer;">&times;</button>
      </div>

      <div class="form-group">
        <label class="form-label">Step Type</label>
        <select class="form-input" id="branch-step-type" onchange="showBranchStepForm(this.value)">
          <option value="navigate">Navigate</option>
          <option value="click">Click</option>
          <option value="type">Type</option>
          <option value="wait">Wait</option>
          <option value="screenshot">Screenshot</option>
          <option value="extract">Extract Text</option>
          <option value="scroll_up">Scroll Up</option>
          <option value="scroll_down">Scroll Down</option>
          <option value="press_key">Press Key</option>
          <option value="highlight">Highlight</option>
        </select>
      </div>

      <!-- Navigate form -->
      <div class="branch-step-form" data-type="navigate">
        <div class="form-group">
          <label class="form-label">URL</label>
          <input type="text" class="form-input" id="branch-navigate-url" placeholder="https://example.com">
        </div>
      </div>

      <!-- Click form -->
      <div class="branch-step-form" data-type="click" style="display: none;">
        <div class="form-group">
          <label class="form-label">Selector</label>
          <input type="text" class="form-input" id="branch-click-selector" placeholder="e.g., Submit button">
        </div>
      </div>

      <!-- Type form -->
      <div class="branch-step-form" data-type="type" style="display: none;">
        <div class="form-group">
          <label class="form-label">Selector</label>
          <input type="text" class="form-input" id="branch-type-selector" placeholder="e.g., Email input">
        </div>
        <div class="form-group">
          <label class="form-label">Text</label>
          <input type="text" class="form-input" id="branch-type-text" placeholder="Text to type">
        </div>
      </div>

      <!-- Wait form -->
      <div class="branch-step-form" data-type="wait" style="display: none;">
        <div class="form-group">
          <label class="form-label">Duration (ms)</label>
          <input type="number" class="form-input" id="branch-wait-duration" value="1000" min="100">
        </div>
      </div>

      <!-- Screenshot form -->
      <div class="branch-step-form" data-type="screenshot" style="display: none;">
        <p style="color: #888; font-size: 12px;">Takes a viewport screenshot</p>
      </div>

      <!-- Extract form -->
      <div class="branch-step-form" data-type="extract" style="display: none;">
        <div class="form-group">
          <label class="form-label">Selector</label>
          <input type="text" class="form-input" id="branch-extract-selector" placeholder="body" value="body">
        </div>
      </div>

      <!-- Scroll Up form -->
      <div class="branch-step-form" data-type="scroll_up" style="display: none;">
        <p style="color: #888; font-size: 12px;">Scrolls to the top of the page</p>
      </div>

      <!-- Scroll Down form -->
      <div class="branch-step-form" data-type="scroll_down" style="display: none;">
        <p style="color: #888; font-size: 12px;">Scrolls to the bottom of the page</p>
      </div>

      <!-- Press Key form -->
      <div class="branch-step-form" data-type="press_key" style="display: none;">
        <div class="form-group">
          <label class="form-label">Key</label>
          <select class="form-input" id="branch-press-key">
            <option value="Enter">Enter</option>
            <option value="Tab">Tab</option>
            <option value="Escape">Escape</option>
            <option value="Backspace">Backspace</option>
            <option value="ArrowUp">Arrow Up</option>
            <option value="ArrowDown">Arrow Down</option>
            <option value="ArrowLeft">Arrow Left</option>
            <option value="ArrowRight">Arrow Right</option>
          </select>
        </div>
      </div>

      <!-- Highlight form -->
      <div class="branch-step-form" data-type="highlight" style="display: none;">
        <div class="form-group">
          <label class="form-label">Selector</label>
          <input type="text" class="form-input" id="branch-highlight-selector" placeholder="e.g., Login button">
        </div>
      </div>

      <button class="btn btn-success" style="width: 100%; margin-top: 12px;" onclick="addBranchStep(document.getElementById('branch-step-type').value)">
        Add to Branch
      </button>
    </div>
  </div>
</body>
</html>
)HTML";
  return html.str();
}
