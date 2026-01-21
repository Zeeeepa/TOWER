// src/browser-registry.cjs
const fs = require('fs');
const path = require('path');
const os = require('os');

const REGISTRY_DIR = path.join(os.tmpdir(), 'owl_browser_registry');
const LOCK_TIMEOUT = 30000; // 30 seconds

class BrowserRegistry {
  constructor() {
    // Ensure registry directory exists
    if (!fs.existsSync(REGISTRY_DIR)) {
      fs.mkdirSync(REGISTRY_DIR, { recursive: true });
    }
  }

  /**
   * Register a new browser instance
   * @param {string} instanceId - Unique instance identifier
   * @param {number} pid - Process ID
   * @returns {boolean} - True if registered successfully
   */
  register(instanceId, pid) {
    const lockFile = path.join(REGISTRY_DIR, `${instanceId}.lock`);

    try {
      const lockData = {
        instanceId,
        pid,
        timestamp: Date.now(),
        hostname: os.hostname()
      };

      fs.writeFileSync(lockFile, JSON.stringify(lockData, null, 2));
      console.error(`[Registry] Registered instance ${instanceId} with PID ${pid}`);
      return true;
    } catch (err) {
      console.error(`[Registry] Failed to register ${instanceId}:`, err.message);
      return false;
    }
  }

  /**
   * Unregister a browser instance
   * @param {string} instanceId - Instance to unregister
   */
  unregister(instanceId) {
    const lockFile = path.join(REGISTRY_DIR, `${instanceId}.lock`);

    try {
      if (fs.existsSync(lockFile)) {
        fs.unlinkSync(lockFile);
        console.error(`[Registry] Unregistered instance ${instanceId}`);
      }
    } catch (err) {
      console.error(`[Registry] Failed to unregister ${instanceId}:`, err.message);
    }
  }

  /**
   * Check if an instance is still alive
   * @param {string} instanceId - Instance to check
   * @returns {boolean} - True if process is running
   */
  isAlive(instanceId) {
    const lockFile = path.join(REGISTRY_DIR, `${instanceId}.lock`);

    if (!fs.existsSync(lockFile)) {
      return false;
    }

    try {
      const lockData = JSON.parse(fs.readFileSync(lockFile, 'utf8'));

      // Check if process is still running
      try {
        process.kill(lockData.pid, 0); // Signal 0 checks existence
        return true;
      } catch (err) {
        // Process doesn't exist, clean up stale lock
        this.unregister(instanceId);
        return false;
      }
    } catch (err) {
      return false;
    }
  }

  /**
   * Find an available browser instance (for reuse)
   * @returns {string|null} - Instance ID if found, null otherwise
   */
  findAvailable() {
    try {
      const files = fs.readdirSync(REGISTRY_DIR);

      for (const file of files) {
        if (file.endsWith('.lock')) {
          const instanceId = file.replace('.lock', '');
          if (this.isAlive(instanceId)) {
            return instanceId;
          }
        }
      }

      return null;
    } catch (err) {
      return null;
    }
  }

  /**
   * Clean up stale lock files
   */
  cleanup() {
    try {
      const files = fs.readdirSync(REGISTRY_DIR);

      for (const file of files) {
        if (file.endsWith('.lock')) {
          const instanceId = file.replace('.lock', '');
          if (!this.isAlive(instanceId)) {
            this.unregister(instanceId);
          }
        }
      }
    } catch (err) {
      console.error('[Registry] Cleanup failed:', err.message);
    }
  }

  /**
   * List all active instances
   * @returns {Array} - Array of instance info objects
   */
  listInstances() {
    const instances = [];

    try {
      const files = fs.readdirSync(REGISTRY_DIR);

      for (const file of files) {
        if (file.endsWith('.lock')) {
          const lockFile = path.join(REGISTRY_DIR, file);
          const lockData = JSON.parse(fs.readFileSync(lockFile, 'utf8'));

          if (this.isAlive(lockData.instanceId)) {
            instances.push(lockData);
          }
        }
      }
    } catch (err) {
      console.error('[Registry] Failed to list instances:', err.message);
    }

    return instances;
  }
}

module.exports = { BrowserRegistry, REGISTRY_DIR };