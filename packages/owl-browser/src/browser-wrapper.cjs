const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const { BrowserRegistry } = require('./browser-registry.cjs');

class BrowserWrapper {
  constructor(instanceId = null, reuseExisting = false) {
    this.registry = new BrowserRegistry();
    this.registry.cleanup(); // Clean up stale instances on startup

    if (reuseExisting) {
      // Try to find existing instance
      const existingId = this.registry.findAvailable();
      if (existingId) {
        console.error(`[BrowserWrapper] Reusing existing instance: ${existingId}`);
        this.instanceId = existingId;
        this.isReused = true;
        return; // Don't spawn new process
      }
    }

    this.instanceId = instanceId || `browser_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
    this.isReused = false;
    this.browserProcess = null;
    this.commandId = 0;
    this.pendingCommands = new Map();
    this.buffer = '';

    // Register cleanup handlers
    this.setupCleanupHandlers();
  }

  setupCleanupHandlers() {
    // Clean up on process exit
    const cleanup = () => {
      if (this.browserProcess && !this.isReused) {
        console.error(`[BrowserWrapper] Cleaning up instance ${this.instanceId}`);
        this.shutdown();
      }
    };

    // Handle various exit scenarios
    process.on('exit', cleanup);
    process.on('SIGINT', () => {
      cleanup();
      process.exit(0);
    });
    process.on('SIGTERM', () => {
      cleanup();
      process.exit(0);
    });
    process.on('uncaughtException', (err) => {
      console.error('[BrowserWrapper] Uncaught exception:', err);
      cleanup();
      process.exit(1);
    });
  }

  async initialize() {
    // If reusing an existing instance, skip initialization
    if (this.isReused) {
      console.error(`[BrowserWrapper] Using reused instance, skipping initialization`);
      return;
    }

    // Detect platform and use correct browser path
    const platform = process.platform;
    let browserPath;

    if (platform === 'darwin') {
      // macOS: App bundle structure
      browserPath = path.join(__dirname, '../build/Release/owl_browser.app/Contents/MacOS/owl_browser');
    } else if (platform === 'linux') {
      // Linux: Direct binary in build directory
      browserPath = path.join(__dirname, '../build/owl_browser');
    } else if (platform === 'win32') {
      // Windows: .exe in build directory
      browserPath = path.join(__dirname, '../build/owl_browser.exe');
    } else {
      throw new Error(`Unsupported platform: ${platform}`);
    }

    if (!fs.existsSync(browserPath)) {
      throw new Error(`Browser binary not found at: ${browserPath}\nPlatform: ${platform}`);
    }

    console.error(`[BrowserWrapper] Using browser at: ${browserPath}`);


    return new Promise((resolve, reject) => {
      // Pass instance ID as command line argument and environment variable
      this.browserProcess = spawn(browserPath, ['--instance-id', this.instanceId], {
        stdio: ['pipe', 'pipe', 'pipe'],
        detached: false, // Keep as child process for proper cleanup
        env: {
          ...process.env,
          OWL_INSTANCE_ID: this.instanceId  // Also set in environment
        }
      });

      let initialized = false;

      this.browserProcess.stdout.on('data', (data) => {
        this.buffer += data.toString();
        const lines = this.buffer.split('\n');

        // Keep the last incomplete line in buffer
        this.buffer = lines.pop() || '';

        lines.forEach(line => {
          if (line.trim() === 'READY' && !initialized) {
            initialized = true;

            // Register this instance in the registry
            this.registry.register(this.instanceId, this.browserProcess.pid);

            console.error(`[BrowserWrapper] Browser ready (instance: ${this.instanceId}, PID: ${this.browserProcess.pid})`);
            resolve();
          } else {
            this.handleResponse(line);
          }
        });
      });

      this.browserProcess.stderr.on('data', (data) => {
        const output = data.toString();
        // Check stderr for READY as well (in case it's logged there)
        if (output.includes('READY') && !initialized) {
          initialized = true;

          // Register this instance in the registry
          this.registry.register(this.instanceId, this.browserProcess.pid);

          console.error(`[BrowserWrapper] Browser ready from stderr (instance: ${this.instanceId}, PID: ${this.browserProcess.pid})`);
          resolve();
        }
        // Log ALL stderr output (includes [INFO], [WARN], [ERROR], etc.)
        console.error(output.trimEnd());
      });

      this.browserProcess.on('error', (err) => {
        reject(err);
      });

      this.browserProcess.on('exit', (code) => {
        // Unregister when process exits
        this.registry.unregister(this.instanceId);

        if (code !== 0 && code !== null) {
          console.error(`[BrowserWrapper] Browser process exited with code ${code}`);
        }
      });

      // Timeout in case browser doesn't start
      setTimeout(() => reject(new Error('Browser initialization timeout')), 30000);
    });
  }

  handleResponse(line) {
    try {
      const response = JSON.parse(line);
      const pending = this.pendingCommands.get(response.id);

      if (pending) {
        this.pendingCommands.delete(response.id);

        if (response.error) {
          pending.reject(new Error(response.error));
        } else {
          // Check for ActionResult format (has success property)
          // ActionResult: {"success": bool, "status": "code", "message": "...", ...}
          const result = response.result;
          if (result && typeof result === 'object' && 'success' in result) {
            if (!result.success) {
              // Create detailed error with status code and message
              const errorMsg = result.message || `Action failed: ${result.status || 'unknown'}`;
              const error = new Error(errorMsg);
              error.status = result.status;
              error.selector = result.selector;
              error.url = result.url;
              error.httpStatus = result.http_status;
              error.errorCode = result.error_code;
              error.elementCount = result.element_count;
              pending.reject(error);
            } else {
              // Success - resolve with the full result for detailed info
              pending.resolve(result);
            }
          } else {
            // Legacy format - resolve as-is
            pending.resolve(result);
          }
        }
      }
    } catch (e) {
      // Ignore non-JSON lines (logs, etc.)
    }
  }

  async sendCommand(method, params = {}) {
    if (!this.browserProcess) {
      throw new Error('Browser not initialized');
    }

    const id = ++this.commandId;
    const command = { id, method, ...params };

    return new Promise((resolve, reject) => {
      this.pendingCommands.set(id, { resolve, reject });

      const commandStr = JSON.stringify(command);
      if (method === 'type' || method === 'click' || method === 'queryPage') {
        console.error(`[BrowserWrapper] Sending command: ${commandStr.substring(0, 200)}`);
      }
      this.browserProcess.stdin.write(commandStr + '\n');

      // Timeout based on command type
      // Image CAPTCHA solving needs longer timeout for retries with skip
      const timeout = (method === 'solveImageCaptcha') ? 240000 : 30000;
      setTimeout(() => {
        if (this.pendingCommands.has(id)) {
          this.pendingCommands.delete(id);
          reject(new Error(`Command timeout: ${method}`));
        }
      }, timeout);
    });
  }

  async createContext(config = {}) {
    // Extract LLM config
    const llmConfig = {
      llm_enabled: config.llm_enabled,
      llm_use_builtin: config.llm_use_builtin,
      llm_endpoint: config.llm_endpoint,
      llm_model: config.llm_model,
      llm_api_key: config.llm_api_key,
      llm_is_third_party: config.llm_is_third_party
    };

    // Extract proxy config
    const proxyConfig = config.proxy ? {
      proxy_type: config.proxy.type || 'http',
      proxy_host: config.proxy.host || '',
      proxy_port: config.proxy.port || 0,
      proxy_username: config.proxy.username || '',
      proxy_password: config.proxy.password || '',
      proxy_enabled: config.proxy.enabled !== false, // Default to true if proxy provided
      proxy_stealth: config.proxy.stealth !== false, // Default to true for stealth
      proxy_block_webrtc: config.proxy.blockWebrtc !== false, // Default to true
      proxy_spoof_timezone: config.proxy.spoofTimezone || false,
      proxy_spoof_language: config.proxy.spoofLanguage || false,
      proxy_timezone_override: config.proxy.timezoneOverride || '',
      proxy_language_override: config.proxy.languageOverride || '',
      // CA certificate for SSL interception proxies (Charles, mitmproxy, etc.)
      proxy_ca_cert_path: config.proxy.caCertPath || '',
      proxy_trust_custom_ca: config.proxy.trustCustomCa || false,
      // Tor-specific settings for circuit isolation
      // Each context gets a new Tor circuit (different exit node IP) automatically
      is_tor: config.proxy.isTor || false,
      tor_control_port: config.proxy.torControlPort !== undefined ? config.proxy.torControlPort : 0,
      tor_control_password: config.proxy.torControlPassword || ''
    } : {};

    // Extract profile config
    const profileConfig = config.profile_path ? {
      profile_path: config.profile_path
    } : {};

    // Resource blocking config (default: true)
    const resourceConfig = {
      resource_blocking: config.resource_blocking !== false
    };

    // Profile filtering options
    const filterConfig = {};
    if (config.os) {
      filterConfig.os = config.os;
    }
    if (config.gpu) {
      filterConfig.gpu = config.gpu;
    }

    const result = await this.sendCommand('createContext', {
      ...llmConfig,
      ...proxyConfig,
      ...profileConfig,
      ...resourceConfig,
      ...filterConfig
    });
    // Browser returns context_id directly as result, not nested
    return result;
  }

  async releaseContext(contextId) {
    await this.sendCommand('releaseContext', { context_id: contextId });
  }

  async navigate(contextId, url, waitUntil = '', timeout = 30000) {
    const result = await this.sendCommand('navigate', {
      context_id: contextId,
      url,
      wait_until: waitUntil,
      timeout
    });
    return result; // Browser returns true/false directly
  }

  async resolveSelector(contextId, selector) {
    // If it looks like a position format (e.g., "100x200"), use it directly
    if (selector.match(/^\d+x\d+$/)) {
      console.error(`[BrowserWrapper] Using position format: "${selector}"`);
      return selector;
    }

    // If it looks like a CSS selector (starts with #, ., [, or contains >), use it directly
    if (selector.match(/^[#.\[]/) || selector.includes('>') || selector.includes(':')) {
      return selector;
    }

    // Otherwise, use semantic matcher to find element
    console.error(`[BrowserWrapper] Resolving semantic selector: "${selector}"`);
    const result = await this.sendCommand('findElement', {
      context_id: contextId,
      description: selector
    });

    if (!result || !result.matches || result.matches.length === 0) {
      throw new Error(`Could not find element matching: "${selector}"`);
    }

    // DEBUG: Log ALL matches to understand prioritization
    console.error(`[BrowserWrapper] Found ${result.matches.length} matches for "${selector}":`);
    for (let i = 0; i < Math.min(result.matches.length, 5); i++) {
      const m = result.matches[i];
      if (!m || !m.element) {
        console.error(`  [${i}] ERROR: Invalid match data`);
        continue;
      }
      const text = (m.element.text || '').substring(0,20);
      console.error(`  [${i}] conf=${m.confidence.toFixed(2)} tag=${m.element.tag} text="${text}" pos=(${m.element.x},${m.element.y}) size=${m.element.width}x${m.element.height}`);
    }

    const resolvedSelector = result.matches[0].element.selector;
    console.error(`[BrowserWrapper] Using match [0]: "${resolvedSelector}"`);

    // Return the selector of the first (highest confidence) match
    return resolvedSelector;
  }

  async click(contextId, selector) {
    // Position format (e.g., "100x200") is handled directly by browser without resolution
    // CSS selectors and semantic descriptions are resolved via resolveSelector
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('click', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  async dragDrop(contextId, startX, startY, endX, endY, midPoints = []) {
    // mid_points is passed as JSON string for the C++ backend
    const midPointsStr = midPoints.length > 0 ? JSON.stringify(midPoints) : '';
    const result = await this.sendCommand('dragDrop', {
      context_id: contextId,
      start_x: startX,
      start_y: startY,
      end_x: endX,
      end_y: endY,
      mid_points: midPointsStr
    });
    return result;
  }

  async html5DragDrop(contextId, sourceSelector, targetSelector) {
    const result = await this.sendCommand('html5DragDrop', {
      context_id: contextId,
      source_selector: sourceSelector,
      target_selector: targetSelector
    });
    return result;
  }

  async mouseMove(contextId, startX, startY, endX, endY, steps = 0, stopPoints = []) {
    // stop_points is passed as JSON string for the C++ backend
    const stopPointsStr = stopPoints.length > 0 ? JSON.stringify(stopPoints) : '';
    const result = await this.sendCommand('mouseMove', {
      context_id: contextId,
      start_x: startX,
      start_y: startY,
      end_x: endX,
      end_y: endY,
      steps: steps,
      stop_points: stopPointsStr
    });
    return result;
  }

  async type(contextId, selector, text) {
    // Position format (e.g., "100x200") is handled directly by browser without resolution
    // CSS selectors and semantic descriptions are resolved via resolveSelector
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('type', {
      context_id: contextId,
      selector: resolvedSelector,
      text
    });
    return result;
  }

  async pick(contextId, selector, value) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('pick', {
      context_id: contextId,
      selector: resolvedSelector,
      value
    });
    return result;
  }

  async pressKey(contextId, key) {
    const result = await this.sendCommand('pressKey', {
      context_id: contextId,
      key
    });
    return result;
  }

  async submitForm(contextId) {
    const result = await this.sendCommand('submitForm', {
      context_id: contextId
    });
    return result;
  }

  async extractText(contextId, selector) {
    // For text extraction, treat common HTML elements as CSS selectors
    // Common selectors: body, html, main, article, div, p, span, etc.
    const htmlElements = ['body', 'html', 'main', 'article', 'section', 'header', 'footer', 'nav', 'aside', 'div', 'p', 'span', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6'];

    let resolvedSelector = selector;

    // If it's a plain HTML element name or CSS selector, use directly
    if (htmlElements.includes(selector.toLowerCase()) ||
        selector.match(/^[#.\[]/) ||
        selector.includes('>') ||
        selector.includes(':')) {
      // Use as CSS selector directly
      resolvedSelector = selector;
      console.error(`[BrowserWrapper] Using "${selector}" as CSS selector for text extraction`);
    } else {
      // Try semantic matching for descriptive selectors
      try {
        resolvedSelector = await this.resolveSelector(contextId, selector);
        console.error(`[BrowserWrapper] Resolved semantic selector "${selector}" to "${resolvedSelector}"`);
      } catch (err) {
        // If semantic matching fails, use original selector as CSS
        console.error(`[BrowserWrapper] Semantic matching failed for "${selector}", using as CSS selector`);
        resolvedSelector = selector;
      }
    }

    const result = await this.sendCommand('extractText', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result || ''; // Browser returns extracted text directly as string
  }

  async screenshot(contextId, mode = 'viewport', selector = null) {
    const command = {
      context_id: contextId
    };

    // Add mode if specified (default: viewport)
    if (mode && mode !== 'viewport') {
      command.mode = mode;
    }

    // Add selector for element mode
    if (mode === 'element' && selector) {
      // Resolve natural language selector if needed
      command.selector = await this.resolveSelector(contextId, selector);
    }

    const result = await this.sendCommand('screenshot', command);

    // Result is base64 string directly, convert to Buffer
    return Buffer.from(result, 'base64');
  }

  async highlight(contextId, selector, borderColor = "#FF0000", backgroundColor = "rgba(255, 0, 0, 0.2)") {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('highlight', {
      context_id: contextId,
      selector: resolvedSelector,
      border_color: borderColor,
      background_color: backgroundColor
    });
    return result;
  }

  async showGridOverlay(contextId, horizontalLines = 25, verticalLines = 25, lineColor = "rgba(255, 0, 0, 0.15)", textColor = "rgba(255, 0, 0, 0.4)") {
    const result = await this.sendCommand('showGridOverlay', {
      context_id: contextId,
      horizontal_lines: horizontalLines,
      vertical_lines: verticalLines,
      line_color: lineColor,
      text_color: textColor
    });
    return result;
  }

  // ==================== CONTENT EXTRACTION METHODS ====================

  async getHTML(contextId, cleanLevel = 'basic') {
    const result = await this.sendCommand('getHTML', {
      context_id: contextId,
      clean_level: cleanLevel
    });
    return result;
  }

  async getMarkdown(contextId, includeLinks = true, includeImages = true, maxLength = -1) {
    const result = await this.sendCommand('getMarkdown', {
      context_id: contextId,
      include_links: includeLinks,
      include_images: includeImages,
      max_length: maxLength
    });
    return result;
  }

  async extractJSON(contextId, templateName = '') {
    const result = await this.sendCommand('extractJSON', {
      context_id: contextId,
      template_name: templateName
    });
    return result;
  }

  async detectWebsiteType(contextId) {
    const result = await this.sendCommand('detectWebsiteType', {
      context_id: contextId
    });
    return result;
  }

  async listTemplates() {
    const result = await this.sendCommand('listTemplates', {});
    return result;
  }

  /**
   * List all active browser contexts
   * @returns {Promise<string[]>} Array of context IDs
   */
  async listContexts() {
    const result = await this.sendCommand('listContexts', {});
    // Result is already an array of context IDs from the browser
    return Array.isArray(result) ? result : [];
  }

  // ==================== AI INTELLIGENCE METHODS ====================

  async summarizePage(contextId, forceRefresh = false) {
    const result = await this.sendCommand('summarizePage', {
      context_id: contextId,
      force_refresh: forceRefresh
    });
    return result;
  }

  async queryPage(contextId, query) {
    const result = await this.sendCommand('queryPage', {
      context_id: contextId,
      query: query
    });
    return result;
  }

  async getLLMStatus() {
    const result = await this.sendCommand('getLLMStatus', {});
    return result;
  }

  async executeNLA(contextId, command) {
    const result = await this.sendCommand('executeNLA', {
      context_id: contextId,
      query: command
    });
    return result;
  }

  // ==================== BROWSER NAVIGATION & CONTROL METHODS ====================

  async reload(contextId, ignoreCache = false, waitUntil = 'load', timeout = 30000) {
    const result = await this.sendCommand('reload', {
      context_id: contextId,
      ignore_cache: ignoreCache,
      wait_until: waitUntil,
      timeout
    });
    return result;
  }

  async goBack(contextId, waitUntil = 'load', timeout = 30000) {
    const result = await this.sendCommand('goBack', {
      context_id: contextId,
      wait_until: waitUntil,
      timeout
    });
    return result;
  }

  async goForward(contextId, waitUntil = 'load', timeout = 30000) {
    const result = await this.sendCommand('goForward', {
      context_id: contextId,
      wait_until: waitUntil,
      timeout
    });
    return result;
  }

  async canGoBack(contextId) {
    const result = await this.sendCommand('canGoBack', {
      context_id: contextId
    });
    return result;
  }

  async canGoForward(contextId) {
    const result = await this.sendCommand('canGoForward', {
      context_id: contextId
    });
    return result;
  }

  // ==================== SCROLL CONTROL METHODS ====================

  async scrollBy(contextId, x, y) {
    const result = await this.sendCommand('scrollBy', {
      context_id: contextId,
      x: x,
      y: y
    });
    return result;
  }

  async scrollTo(contextId, x, y) {
    const result = await this.sendCommand('scrollTo', {
      context_id: contextId,
      x: x,
      y: y
    });
    return result;
  }

  async scrollToElement(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('scrollToElement', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  async scrollToTop(contextId) {
    const result = await this.sendCommand('scrollToTop', {
      context_id: contextId
    });
    return result;
  }

  async scrollToBottom(contextId) {
    const result = await this.sendCommand('scrollToBottom', {
      context_id: contextId
    });
    return result;
  }

  // ==================== WAIT UTILITIES METHODS ====================

  async waitForSelector(contextId, selector, timeout = 5000) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('waitForSelector', {
      context_id: contextId,
      selector: resolvedSelector,
      timeout: timeout
    });
    return result;
  }

  async waitForTimeout(contextId, timeout) {
    const result = await this.sendCommand('waitForTimeout', {
      context_id: contextId,
      timeout: timeout
    });
    return result;
  }

  async waitForNetworkIdle(contextId, idleTime = 500, timeout = 30000) {
    const result = await this.sendCommand('waitForNetworkIdle', {
      context_id: contextId,
      idle_time: idleTime,
      timeout: timeout
    });
    return result;
  }

  async waitForFunction(contextId, jsFunction, polling = 100, timeout = 30000) {
    const result = await this.sendCommand('waitForFunction', {
      context_id: contextId,
      js_function: jsFunction,
      polling: polling,
      timeout: timeout
    });
    return result;
  }

  async waitForURL(contextId, urlPattern, isRegex = false, timeout = 30000) {
    const result = await this.sendCommand('waitForURL', {
      context_id: contextId,
      url_pattern: urlPattern,
      is_regex: isRegex,
      timeout: timeout
    });
    return result;
  }

  // ==================== PAGE STATE QUERY METHODS ====================

  async getCurrentURL(contextId) {
    const result = await this.sendCommand('getCurrentURL', {
      context_id: contextId
    });
    return result;
  }

  async getPageTitle(contextId) {
    const result = await this.sendCommand('getPageTitle', {
      context_id: contextId
    });
    return result;
  }

  async getPageInfo(contextId) {
    const result = await this.sendCommand('getPageInfo', {
      context_id: contextId
    });
    return result;
  }

  // ==================== VIEWPORT MANIPULATION METHODS ====================

  async setViewport(contextId, width, height) {
    const result = await this.sendCommand('setViewport', {
      context_id: contextId,
      width: width,
      height: height
    });
    return result;
  }

  async getViewport(contextId) {
    const result = await this.sendCommand('getViewport', {
      context_id: contextId
    });
    return result;
  }

  // ==================== VIDEO RECORDING METHODS - NEW! ====================

  async startVideoRecording(contextId, fps = 30, codec = 'libx264') {
    const result = await this.sendCommand('startVideoRecording', {
      context_id: contextId,
      fps: fps,
      codec: codec
    });
    return result;
  }

  async pauseVideoRecording(contextId) {
    const result = await this.sendCommand('pauseVideoRecording', {
      context_id: contextId
    });
    return result;
  }

  async resumeVideoRecording(contextId) {
    const result = await this.sendCommand('resumeVideoRecording', {
      context_id: contextId
    });
    return result;
  }

  async stopVideoRecording(contextId) {
    const result = await this.sendCommand('stopVideoRecording', {
      context_id: contextId
    });
    return result;
  }

  async getVideoRecordingStats(contextId) {
    const result = await this.sendCommand('getVideoRecordingStats', {
      context_id: contextId
    });
    return result;
  }

  async shutdown() {
    // Unregister before shutdown
    if (!this.isReused) {
      this.registry.unregister(this.instanceId);
    }

    if (this.browserProcess) {
      const pid = this.browserProcess.pid;
      console.error(`[BrowserWrapper] Shutting down browser process (PID: ${pid})...`);

      // Send shutdown command to browser (graceful)
      try {
        await this.sendCommand('shutdown', {});
        console.error('[BrowserWrapper] Sent shutdown command');
      } catch (err) {
        console.error('[BrowserWrapper] Failed to send shutdown command:', err.message);
      }

      // Close stdin
      this.browserProcess.stdin.end();

      // Wait for graceful termination (3 seconds)
      const waitForExit = new Promise((resolve) => {
        const timeout = setTimeout(() => {
          console.error('[BrowserWrapper] Browser did not exit gracefully, force killing...');
          resolve(false);
        }, 3000);

        this.browserProcess.once('exit', () => {
          clearTimeout(timeout);
          console.error('[BrowserWrapper] Browser exited gracefully');
          resolve(true);
        });
      });

      const graceful = await waitForExit;

      // Force kill if still alive
      if (!graceful) {
        try {
          process.kill(pid, 'SIGKILL');
          console.error(`[BrowserWrapper] Force killed browser process (PID: ${pid})`);
        } catch (err) {
          // Process already dead
          console.error('[BrowserWrapper] Process already terminated');
        }
      }

      this.browserProcess = null;
      console.error('[BrowserWrapper] Shutdown complete');
    }
  }

  // ==================== DEMOGRAPHICS AND CONTEXT METHODS ====================

  async getDemographics() {
    const result = await this.sendCommand('getDemographics');
    return result;
  }

  async getLocation() {
    const result = await this.sendCommand('getLocation');
    return result;
  }

  async getDateTime() {
    const result = await this.sendCommand('getDateTime');
    return result;
  }

  async getWeather() {
    const result = await this.sendCommand('getWeather');
    return result;
  }

  // ==================== CAPTCHA HANDLING METHODS ====================

  async detectCaptcha(contextId) {
    const result = await this.sendCommand('detectCaptcha', { context_id: contextId });
    return result;
  }

  async classifyCaptcha(contextId) {
    const result = await this.sendCommand('classifyCaptcha', { context_id: contextId });
    return result;
  }

  async solveTextCaptcha(contextId, maxAttempts = 3) {
    const result = await this.sendCommand('solveTextCaptcha', {
      context_id: contextId,
      max_attempts: maxAttempts
    });
    return result;
  }

  async solveImageCaptcha(contextId, maxAttempts = 3, provider = 'auto') {
    const result = await this.sendCommand('solveImageCaptcha', {
      context_id: contextId,
      max_attempts: maxAttempts,
      provider: provider
    });
    return result;
  }

  async solveCaptcha(contextId, maxAttempts = 3, provider = 'auto') {
    const result = await this.sendCommand('solveCaptcha', {
      context_id: contextId,
      max_attempts: maxAttempts,
      provider: provider
    });
    return result;
  }

  // ==================== COOKIE MANAGEMENT METHODS ====================

  async getCookies(contextId, url = '') {
    const result = await this.sendCommand('getCookies', {
      context_id: contextId,
      url: url
    });
    return result;
  }

  async setCookie(contextId, url, name, value, options = {}) {
    const result = await this.sendCommand('setCookie', {
      context_id: contextId,
      url: url,
      name: name,
      value: value,
      domain: options.domain || '',
      path: options.path || '/',
      secure: options.secure || false,
      http_only: options.httpOnly || false,
      same_site: options.sameSite || 'lax',
      expires: options.expires || -1
    });
    return result;
  }

  async deleteCookies(contextId, url = '', cookieName = '') {
    const result = await this.sendCommand('deleteCookies', {
      context_id: contextId,
      url: url,
      cookie_name: cookieName
    });
    return result;
  }

  // ==================== PROXY MANAGEMENT METHODS ====================

  /**
   * Set proxy configuration for a browser context
   * @param {string} contextId - The browser context ID
   * @param {object} proxyConfig - Proxy configuration
   * @param {string} proxyConfig.type - Proxy type: 'http', 'https', 'socks4', 'socks5', 'socks5h'
   * @param {string} proxyConfig.host - Proxy server hostname or IP
   * @param {number} proxyConfig.port - Proxy server port
   * @param {string} [proxyConfig.username] - Authentication username
   * @param {string} [proxyConfig.password] - Authentication password
   * @param {boolean} [proxyConfig.enabled=true] - Enable/disable proxy
   * @param {boolean} [proxyConfig.stealth=true] - Enable stealth mode (WebRTC blocking, etc.)
   * @param {boolean} [proxyConfig.blockWebrtc=true] - Block WebRTC to prevent IP leaks
   * @param {boolean} [proxyConfig.spoofTimezone=false] - Spoof timezone based on proxy location
   * @param {boolean} [proxyConfig.spoofLanguage=false] - Spoof language based on proxy location
   * @param {string} [proxyConfig.timezoneOverride] - Manual timezone override (e.g., 'America/New_York')
   * @param {string} [proxyConfig.languageOverride] - Manual language override (e.g., 'en-US')
   */
  async setProxy(contextId, proxyConfig = {}) {
    const result = await this.sendCommand('setProxy', {
      context_id: contextId,
      proxy_type: proxyConfig.type || 'http',
      proxy_host: proxyConfig.host || '',
      proxy_port: proxyConfig.port || 0,
      proxy_username: proxyConfig.username || '',
      proxy_password: proxyConfig.password || '',
      proxy_enabled: proxyConfig.enabled !== false,
      proxy_stealth: proxyConfig.stealth !== false,
      proxy_block_webrtc: proxyConfig.blockWebrtc !== false,
      proxy_spoof_timezone: proxyConfig.spoofTimezone || false,
      proxy_spoof_language: proxyConfig.spoofLanguage || false,
      proxy_timezone_override: proxyConfig.timezoneOverride || '',
      proxy_language_override: proxyConfig.languageOverride || ''
    });
    return result;
  }

  /**
   * Get current proxy status for a browser context
   * @param {string} contextId - The browser context ID
   * @returns {object} Proxy status including enabled, type, host, port, stealth settings, and connection status
   */
  async getProxyStatus(contextId) {
    const result = await this.sendCommand('getProxyStatus', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Connect/enable proxy for a browser context
   * @param {string} contextId - The browser context ID
   * @returns {boolean} Success status
   */
  async connectProxy(contextId) {
    const result = await this.sendCommand('connectProxy', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Disconnect/disable proxy for a browser context
   * @param {string} contextId - The browser context ID
   * @returns {boolean} Success status
   */
  async disconnectProxy(contextId) {
    const result = await this.sendCommand('disconnectProxy', {
      context_id: contextId
    });
    return result;
  }

  // ====== Profile Management Methods ======

  /**
   * Create a new browser profile with randomized fingerprints
   * @param {string} profileName - Optional human-readable name for the profile
   * @returns {string} Profile JSON
   */
  async createProfile(profileName = '') {
    const result = await this.sendCommand('createProfile', {
      name: profileName
    });
    return result;
  }

  /**
   * Load a browser profile from a JSON file into an existing context
   * @param {string} contextId - The browser context ID
   * @param {string} profilePath - Path to the profile JSON file
   * @returns {string} Profile JSON
   */
  async loadProfile(contextId, profilePath) {
    const result = await this.sendCommand('loadProfile', {
      context_id: contextId,
      profile_path: profilePath
    });
    return result;
  }

  /**
   * Save the current context state to a profile JSON file
   * @param {string} contextId - The browser context ID
   * @param {string} profilePath - Path to save the profile JSON file (optional)
   * @returns {string} Profile JSON
   */
  async saveProfile(contextId, profilePath = '') {
    const result = await this.sendCommand('saveProfile', {
      context_id: contextId,
      profile_path: profilePath
    });
    return result;
  }

  /**
   * Get the current profile state for a context as JSON
   * @param {string} contextId - The browser context ID
   * @returns {string} Profile JSON
   */
  async getProfile(contextId) {
    const result = await this.sendCommand('getProfile', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Update the profile file with the current cookies from the browser
   * @param {string} contextId - The browser context ID
   * @returns {boolean} Success status
   */
  async updateProfileCookies(contextId) {
    const result = await this.sendCommand('updateProfileCookies', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Get context information including the VM profile and fingerprint hashes
   * @param {string} contextId - The browser context ID
   * @returns {string} JSON string with context info (vm_profile, canvas, audio, gpu hashes)
   */
  async getContextInfo(contextId) {
    const result = await this.sendCommand('getContextInfo', {
      context_id: contextId
    });
    return result;
  }

  // ==================== ADVANCED MOUSE INTERACTIONS ====================

  /**
   * Hover over an element without clicking
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector, position (e.g., "100x200"), or semantic description
   * @returns {boolean} Success status
   */
  async hover(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('hover', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Double-click an element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector, position, or semantic description
   * @returns {boolean} Success status
   */
  async doubleClick(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('doubleClick', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Right-click an element (context menu)
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector, position, or semantic description
   * @returns {boolean} Success status
   */
  async rightClick(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('rightClick', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  // ==================== INPUT CONTROL ====================

  /**
   * Clear an input field
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Success status
   */
  async clearInput(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('clearInput', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Focus on an element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Success status
   */
  async focus(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('focus', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Blur (unfocus) an element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Success status
   */
  async blur(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('blur', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Select all text in an element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Success status
   */
  async selectAll(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('selectAll', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  // ==================== KEYBOARD COMBINATIONS ====================

  /**
   * Press a keyboard combination (e.g., "Ctrl+A", "Shift+Enter")
   * @param {string} contextId - The browser context ID
   * @param {string} combo - Key combination string
   * @returns {boolean} Success status
   */
  async keyboardCombo(contextId, combo) {
    const result = await this.sendCommand('keyboardCombo', {
      context_id: contextId,
      combo: combo
    });
    return result;
  }

  // ==================== JAVASCRIPT EVALUATION ====================

  /**
   * Execute JavaScript in the page context
   * @param {string} contextId - The browser context ID
   * @param {string} script - JavaScript code to execute
   * @param {boolean} returnValue - If true, treats script as expression and returns its value
   * @returns {object} Execution result
   */
  async evaluate(contextId, script, returnValue = false) {
    const result = await this.sendCommand('evaluate', {
      context_id: contextId,
      script: script,
      return_value: returnValue
    });
    return result;
  }

  // ==================== ELEMENT STATE CHECKS ====================

  /**
   * Check if an element is visible
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Whether element is visible
   */
  async isVisible(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('isVisible', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Check if an element is enabled
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Whether element is enabled
   */
  async isEnabled(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('isEnabled', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Check if a checkbox/radio is checked
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {boolean} Whether element is checked
   */
  async isChecked(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('isChecked', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  /**
   * Get an attribute value from an element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @param {string} attribute - Attribute name
   * @returns {string} Attribute value
   */
  async getAttribute(contextId, selector, attribute) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('getAttribute', {
      context_id: contextId,
      selector: resolvedSelector,
      attribute: attribute
    });
    return result;
  }

  /**
   * Get the bounding box of an element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector or semantic description
   * @returns {object} Bounding box {x, y, width, height}
   */
  async getBoundingBox(contextId, selector) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('getBoundingBox', {
      context_id: contextId,
      selector: resolvedSelector
    });
    return result;
  }

  // ==================== FILE OPERATIONS ====================

  /**
   * Upload files to a file input element
   * @param {string} contextId - The browser context ID
   * @param {string} selector - CSS selector for file input
   * @param {string[]} filePaths - Array of file paths to upload
   * @returns {boolean} Success status
   */
  async uploadFile(contextId, selector, filePaths) {
    const resolvedSelector = await this.resolveSelector(contextId, selector);
    const result = await this.sendCommand('uploadFile', {
      context_id: contextId,
      selector: resolvedSelector,
      file_paths: JSON.stringify(filePaths)
    });
    return result;
  }

  // ==================== FRAME/IFRAME HANDLING ====================

  /**
   * List all frames in the page
   * @param {string} contextId - The browser context ID
   * @returns {object[]} Array of frame info objects
   */
  async listFrames(contextId) {
    const result = await this.sendCommand('listFrames', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Switch to an iframe
   * @param {string} contextId - The browser context ID
   * @param {string} frameSelector - Frame name, index, or CSS selector
   * @returns {boolean} Success status
   */
  async switchToFrame(contextId, frameSelector) {
    const result = await this.sendCommand('switchToFrame', {
      context_id: contextId,
      frame_selector: frameSelector
    });
    return result;
  }

  /**
   * Switch back to the main frame
   * @param {string} contextId - The browser context ID
   * @returns {boolean} Success status
   */
  async switchToMainFrame(contextId) {
    const result = await this.sendCommand('switchToMainFrame', {
      context_id: contextId
    });
    return result;
  }

  // ==================== NETWORK INTERCEPTION ====================

  /**
   * Add a network interception rule
   * @param {string} contextId - The browser context ID
   * @param {object} rule - Rule object {url_pattern, action, is_regex, redirect_url, mock_body, mock_status, mock_content_type}
   * @returns {object} Result with rule_id
   */
  async addNetworkRule(contextId, rule) {
    const result = await this.sendCommand('addNetworkRule', {
      context_id: contextId,
      rule_json: JSON.stringify(rule)
    });
    return result;
  }

  /**
   * Remove a network interception rule
   * @param {string} ruleId - The rule ID to remove
   * @returns {boolean} Success status
   */
  async removeNetworkRule(ruleId) {
    const result = await this.sendCommand('removeNetworkRule', {
      rule_id: ruleId
    });
    return result;
  }

  /**
   * Enable or disable network interception
   * @param {string} contextId - The browser context ID
   * @param {boolean} enable - Enable or disable
   * @returns {boolean} Success status
   */
  async enableNetworkInterception(contextId, enable) {
    const result = await this.sendCommand('enableNetworkInterception', {
      context_id: contextId,
      enable: enable
    });
    return result;
  }

  /**
   * Enable or disable network logging
   * @param {string} contextId - The browser context ID
   * @param {boolean} enable - Enable or disable
   * @returns {boolean} Success status
   */
  async enableNetworkLogging(contextId, enable) {
    const result = await this.sendCommand('enableNetworkLogging', {
      context_id: contextId,
      enable: enable
    });
    return result;
  }

  /**
   * Get captured network log
   * @param {string} contextId - The browser context ID
   * @returns {object} Network log with requests and responses
   */
  async getNetworkLog(contextId) {
    const result = await this.sendCommand('getNetworkLog', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Clear the network log
   * @param {string} contextId - The browser context ID
   * @returns {boolean} Success status
   */
  async clearNetworkLog(contextId) {
    const result = await this.sendCommand('clearNetworkLog', {
      context_id: contextId
    });
    return result;
  }

  // ==================== DOWNLOAD MANAGEMENT ====================

  /**
   * Set download path for automatic downloads
   * @param {string} contextId - The browser context ID
   * @param {string} path - Directory path for downloads
   * @returns {boolean} Success status
   */
  async setDownloadPath(contextId, path) {
    const result = await this.sendCommand('setDownloadPath', {
      context_id: contextId,
      download_path: path
    });
    return result;
  }

  /**
   * Get all downloads for a context
   * @param {string} contextId - The browser context ID
   * @returns {object[]} Array of download info
   */
  async getDownloads(contextId) {
    const result = await this.sendCommand('getDownloads', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Get active/in-progress downloads
   * @param {string} contextId - The browser context ID
   * @returns {object[]} Array of active download info
   */
  async getActiveDownloads(contextId) {
    const result = await this.sendCommand('getActiveDownloads', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Wait for a download to complete
   * @param {string} downloadId - The download ID
   * @param {number} timeout - Timeout in milliseconds
   * @returns {boolean} Success status
   */
  async waitForDownload(downloadId, timeout = 30000) {
    const result = await this.sendCommand('waitForDownload', {
      download_id: downloadId,
      timeout: timeout
    });
    return result;
  }

  /**
   * Cancel a download
   * @param {string} downloadId - The download ID
   * @returns {boolean} Success status
   */
  async cancelDownload(downloadId) {
    const result = await this.sendCommand('cancelDownload', {
      download_id: downloadId
    });
    return result;
  }

  // ==================== DIALOG HANDLING ====================

  /**
   * Set automatic action for dialogs (alert, confirm, prompt, beforeunload)
   * @param {string} contextId - The browser context ID
   * @param {string} dialogType - Dialog type: 'alert', 'confirm', 'prompt', 'beforeunload'
   * @param {string} action - Action: 'accept', 'dismiss', 'accept_with_text'
   * @param {string} promptText - Default text for prompt dialogs
   * @returns {boolean} Success status
   */
  async setDialogAction(contextId, dialogType, action, promptText = '') {
    const result = await this.sendCommand('setDialogAction', {
      context_id: contextId,
      dialog_type: dialogType,
      action: action,
      prompt_text: promptText
    });
    return result;
  }

  /**
   * Get the pending dialog info
   * @param {string} contextId - The browser context ID
   * @returns {object} Pending dialog info or empty object
   */
  async getPendingDialog(contextId) {
    const result = await this.sendCommand('getPendingDialog', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Handle a specific dialog
   * @param {string} dialogId - The dialog ID
   * @param {boolean} accept - Accept or dismiss
   * @param {string} responseText - Response text for prompt dialogs
   * @returns {boolean} Success status
   */
  async handleDialog(dialogId, accept, responseText = '') {
    const result = await this.sendCommand('handleDialog', {
      dialog_id: dialogId,
      accept: accept,
      response_text: responseText
    });
    return result;
  }

  /**
   * Wait for a dialog to appear
   * @param {string} contextId - The browser context ID
   * @param {number} timeout - Timeout in milliseconds
   * @returns {boolean} Whether a dialog appeared
   */
  async waitForDialog(contextId, timeout = 5000) {
    const result = await this.sendCommand('waitForDialog', {
      context_id: contextId,
      timeout: timeout
    });
    return result;
  }

  /**
   * Get all dialogs for a context
   * @param {string} contextId - The browser context ID
   * @returns {object[]} Array of dialog info
   */
  async getDialogs(contextId) {
    const result = await this.sendCommand('getDialogs', {
      context_id: contextId
    });
    return result;
  }

  // ==================== TAB/WINDOW MANAGEMENT ====================

  /**
   * Set popup handling policy
   * @param {string} contextId - The browser context ID
   * @param {string} policy - Policy: 'allow', 'block', 'new_tab', 'background'
   * @returns {boolean} Success status
   */
  async setPopupPolicy(contextId, policy) {
    const result = await this.sendCommand('setPopupPolicy', {
      context_id: contextId,
      popup_policy: policy
    });
    return result;
  }

  /**
   * Get all tabs for a context
   * @param {string} contextId - The browser context ID
   * @returns {object[]} Array of tab info
   */
  async getTabs(contextId) {
    const result = await this.sendCommand('getTabs', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Switch to a specific tab
   * @param {string} contextId - The browser context ID
   * @param {string} tabId - The tab ID to switch to
   * @returns {boolean} Success status
   */
  async switchTab(contextId, tabId) {
    const result = await this.sendCommand('switchTab', {
      context_id: contextId,
      tab_id: tabId
    });
    return result;
  }

  /**
   * Close a specific tab
   * @param {string} contextId - The browser context ID
   * @param {string} tabId - The tab ID to close
   * @returns {boolean} Success status
   */
  async closeTab(contextId, tabId) {
    const result = await this.sendCommand('closeTab', {
      context_id: contextId,
      tab_id: tabId
    });
    return result;
  }

  /**
   * Open a new tab
   * @param {string} contextId - The browser context ID
   * @param {string} url - Optional URL to navigate to
   * @returns {object} Result with tab_id
   */
  async newTab(contextId, url = '') {
    const result = await this.sendCommand('newTab', {
      context_id: contextId,
      url: url
    });
    return result;
  }

  /**
   * Get the active tab ID
   * @param {string} contextId - The browser context ID
   * @returns {object} Result with tab_id
   */
  async getActiveTab(contextId) {
    const result = await this.sendCommand('getActiveTab', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Get the number of open tabs
   * @param {string} contextId - The browser context ID
   * @returns {object} Result with count
   */
  async getTabCount(contextId) {
    const result = await this.sendCommand('getTabCount', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Get blocked popup URLs
   * @param {string} contextId - The browser context ID
   * @returns {string[]} Array of blocked popup URLs
   */
  async getBlockedPopups(contextId) {
    const result = await this.sendCommand('getBlockedPopups', {
      context_id: contextId
    });
    return result;
  }

  // ==================== CLIPBOARD MANAGEMENT ====================

  /**
   * Read text content from the system clipboard
   * @param {string} contextId - The browser context ID
   * @returns {object} Object with text property containing clipboard content
   */
  async clipboardRead(contextId) {
    const result = await this.sendCommand('clipboardRead', {
      context_id: contextId
    });
    return result;
  }

  /**
   * Write text content to the system clipboard
   * @param {string} contextId - The browser context ID
   * @param {string} text - Text to write to clipboard
   */
  async clipboardWrite(contextId, text) {
    const result = await this.sendCommand('clipboardWrite', {
      context_id: contextId,
      text: text
    });
    return result;
  }

  /**
   * Clear the system clipboard content
   * @param {string} contextId - The browser context ID
   */
  async clipboardClear(contextId) {
    const result = await this.sendCommand('clipboardClear', {
      context_id: contextId
    });
    return result;
  }

  // ==================== CONSOLE LOG MANAGEMENT ====================

  /**
   * Get console logs from the browser
   * @param {string} contextId - The browser context ID
   * @param {string} level - Filter by log level (debug, info, warn, error, verbose)
   * @param {string} filter - Filter logs containing specific text
   * @param {number} limit - Maximum number of log entries to return
   * @returns {object} Object with logs array
   */
  async getConsoleLogs(contextId, level, filter, limit) {
    const params = { context_id: contextId };
    if (level) params.level = level;
    if (filter) params.filter = filter;
    if (limit) params.limit = limit;
    const result = await this.sendCommand('getConsoleLogs', params);
    return result;
  }

  /**
   * Clear all console logs from the browser
   * @param {string} contextId - The browser context ID
   */
  async clearConsoleLogs(contextId) {
    const result = await this.sendCommand('clearConsoleLogs', {
      context_id: contextId
    });
    return result;
  }
}

module.exports = { BrowserWrapper };