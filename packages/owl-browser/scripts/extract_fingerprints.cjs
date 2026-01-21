/**
 * Extract fingerprint.com reports from multiple browser contexts
 * Saves full JSON reports to fingerprint_results/
 *
 * Usage: node scripts/extract_fingerprints.cjs [num_contexts] [--no-clean]
 *   num_contexts: Number of contexts to create (default: 3)
 *   --no-clean:   Keep previous reports (default: delete them)
 */

const { spawn, execSync } = require('child_process');
const path = require('path');
const readline = require('readline');
const fs = require('fs');

// Parse arguments
const args = process.argv.slice(2);
const noClean = args.includes('--no-clean') || args.includes('--keep');
const numArg = args.find(a => !a.startsWith('--') && !isNaN(parseInt(a)));
const NUM_CONTEXTS = numArg ? parseInt(numArg) : 3;
const OUTPUT_DIR = path.join(__dirname, '../fingerprint_results');

// Kill any lingering owl_browser processes
try {
  execSync('pkill -9 -f owl_browser 2>/dev/null || true', { stdio: 'ignore' });
} catch (e) {
  // Ignore errors - no processes to kill
}

// Ensure output directory exists
if (!fs.existsSync(OUTPUT_DIR)) {
  fs.mkdirSync(OUTPUT_DIR, { recursive: true });
}

// Clean previous reports unless --no-clean is specified
if (!noClean) {
  const existingFiles = fs.readdirSync(OUTPUT_DIR).filter(f => f.endsWith('.json'));
  if (existingFiles.length > 0) {
    console.log(`Cleaning ${existingFiles.length} previous report(s)...`);
    existingFiles.forEach(f => fs.unlinkSync(path.join(OUTPUT_DIR, f)));
  }
}

// Start MCP server
const mcpServer = spawn('node', [path.join(__dirname, '../src/mcp-server.cjs')]);

const rl = readline.createInterface({
  input: mcpServer.stdout,
  crlfDelay: Infinity
});

let requestId = 1;
const pendingRequests = new Map();

// Handle responses from MCP server
rl.on('line', (line) => {
  try {
    const response = JSON.parse(line);
    if (response.id) {
      const pending = pendingRequests.get(response.id);
      if (pending) {
        pendingRequests.delete(response.id);
        if (response.error) {
          pending.reject(new Error(response.error.message || 'Unknown error'));
        } else {
          pending.resolve(response.result);
        }
      }
    }
  } catch (e) {
    // Ignore parse errors (debug output)
  }
});

mcpServer.stderr.on('data', (data) => {
  const msg = data.toString();
  if (msg.includes('Owl Browser MCP Server')) {
    console.log('[SERVER READY]');
    setTimeout(runExtraction, 1000);
  }
});

function sendMCPRequest(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = requestId++;
    const request = {
      jsonrpc: '2.0',
      id,
      method,
      params,
    };

    pendingRequests.set(id, { resolve, reject });
    mcpServer.stdin.write(JSON.stringify(request) + '\n');

    setTimeout(() => {
      if (pendingRequests.has(id)) {
        pendingRequests.delete(id);
        reject(new Error(`Request ${method} timed out`));
      }
    }, 60000);
  });
}

async function callTool(name, args = {}) {
  const result = await sendMCPRequest('tools/call', { name, arguments: args });
  return result.content?.[0]?.text || '';
}

async function extractFingerprint(contextNum) {
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-');

  console.log(`\n--- Context ${contextNum}/${NUM_CONTEXTS} ---`);

  // Create context
  console.log('  Creating context...');
  const createResult = await callTool('browser_create_context', { llm_enabled: false });
  const match = createResult.match(/ctx_\d+/);
  const context_id = match ? match[0] : null;
  if (!context_id) throw new Error('Failed to create context');
  console.log(`  Context: ${context_id}`);

  try {
    // Navigate
    console.log('  Navigating to fingerprint.com...');
    await callTool('browser_navigate', {
      context_id,
      url: 'https://demo.fingerprint.com/playground',
      wait_until: 'networkidle'
    });

    // Wait for fingerprint detection to complete
    console.log('  Waiting for detection...');
    await new Promise(r => setTimeout(r, 5000));

    // Scroll to JSON section
    console.log('  Scrolling to JSON...');
    await callTool('browser_scroll_by', { context_id, y: 1800 });
    await new Promise(r => setTimeout(r, 1000));

    // Click copy button (Server API Response panel)
    console.log('  Clicking copy button...');
    await callTool('browser_click', { context_id, selector: '1550x363' });
    await new Promise(r => setTimeout(r, 1000));

    // Get parsed JSON via evaluate
    console.log('  Extracting JSON...');
    const jsonResult = await callTool('browser_evaluate', {
      context_id,
      script: `JSON.stringify(JSON.parse(window[Symbol.for('__owl_clipboard__')]), null, 2)`,
      return_value: true
    });

    // Parse and save
    let jsonData;
    try {
      // Result comes back as quoted string
      jsonData = JSON.parse(jsonResult);
    } catch {
      jsonData = jsonResult;
    }

    const filename = `fingerprint_${context_id}_${timestamp}.json`;
    const filepath = path.join(OUTPUT_DIR, filename);
    fs.writeFileSync(filepath, typeof jsonData === 'string' ? jsonData : JSON.stringify(jsonData, null, 2));
    console.log(`  Saved: ${filename}`);

    // Extract key detection values
    try {
      const data = typeof jsonData === 'string' ? JSON.parse(jsonData) : jsonData;
      const botResult = data.products?.botd?.data?.bot?.result || 'unknown';
      const botType = data.products?.botd?.data?.bot?.type || 'none';
      const tampering = data.products?.tampering?.data?.result || false;
      const suspectScore = data.products?.suspectScore?.data?.result || 0;
      const canvasGeo = data.products?.rawDeviceAttributes?.data?.canvas?.value?.Geometry || 'N/A';
      const canvasText = data.products?.rawDeviceAttributes?.data?.canvas?.value?.Text || 'N/A';
      const webglExt = data.products?.rawDeviceAttributes?.data?.webGlExtensions?.value || {};

      console.log(`  Bot: ${botResult} (${botType}), Tampering: ${tampering}, Suspect: ${suspectScore}`);
      console.log(`  Canvas: Geo=${canvasGeo.slice(0,8)}... Text=${canvasText.slice(0,8)}...`);
      console.log(`  WebGL Ext: params=${(webglExt.extensionParameters || 'N/A').slice(0,8)}... ctx=${(webglExt.contextAttributes || 'N/A').slice(0,8)}...`);
    } catch (e) {
      console.log('  (Could not parse detection values)');
    }

  } finally {
    // Always close context
    await callTool('browser_close_context', { context_id });
    console.log('  Context closed');
  }
}

async function runExtraction() {
  try {
    console.log(`\n=== Fingerprint Extraction (${NUM_CONTEXTS} contexts) ===`);
    console.log(`Output: ${OUTPUT_DIR}\n`);

    for (let i = 1; i <= NUM_CONTEXTS; i++) {
      await extractFingerprint(i);
    }

    console.log('\n=== Extraction Complete ===');
    console.log(`Reports saved to: ${OUTPUT_DIR}`);

    mcpServer.kill();
    process.exit(0);

  } catch (error) {
    console.error('\nExtraction failed:', error.message);
    mcpServer.kill();
    process.exit(1);
  }
}
