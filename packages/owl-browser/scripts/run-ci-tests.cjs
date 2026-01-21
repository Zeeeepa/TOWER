#!/usr/bin/env node
/**
 * CI Test Runner for Owl Browser
 *
 * Usage:
 *   node scripts/run-ci-tests.cjs <suite>
 *   node scripts/run-ci-tests.cjs smoke
 *   node scripts/run-ci-tests.cjs core
 *   node scripts/run-ci-tests.cjs all
 *
 * Options:
 *   --verbose, -v     Show detailed output
 *   --continue, -c    Continue on failure (don't exit on first failure)
 *   --list, -l        List available test suites
 *   --dry-run         Show tests that would run without executing
 */

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

// Load configuration
const configPath = path.join(__dirname, '..', 'tests', 'ci-config.json');
let config;

try {
  config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
} catch (e) {
  console.error(`ERROR: Could not load test configuration from ${configPath}`);
  console.error(e.message);
  process.exit(1);
}

// Parse arguments
const args = process.argv.slice(2);
const flags = {
  verbose: args.includes('--verbose') || args.includes('-v'),
  continue: args.includes('--continue') || args.includes('-c'),
  list: args.includes('--list') || args.includes('-l'),
  dryRun: args.includes('--dry-run'),
};

// Remove flags to get suite name
const suiteArg = args.find(a => !a.startsWith('-'));
const suiteName = suiteArg || 'smoke';

// Colors for terminal output
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  dim: '\x1b[2m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m',
};

function log(message, color = '') {
  console.log(`${color}${message}${colors.reset}`);
}

function listSuites() {
  log('\nAvailable test suites:', colors.bright);
  log('='.repeat(60));

  for (const [name, suite] of Object.entries(config.suites)) {
    const status = suite.required ? `${colors.green}[required]${colors.reset}` : `${colors.yellow}[optional]${colors.reset}`;
    log(`\n${colors.cyan}${name}${colors.reset} ${status}`);
    log(`  ${suite.description}`);
    log(`  Tests: ${suite.tests.length}`);
    log(`  Timeout: ${suite.timeout / 1000}s`);

    if (flags.verbose) {
      suite.tests.forEach(t => log(`    - ${t}`, colors.dim));
    }
  }

  log('\n' + '='.repeat(60));
  log('Run with: node scripts/run-ci-tests.cjs <suite>');
}

async function runTest(testFile, timeout, env = {}) {
  return new Promise((resolve) => {
    const testPath = path.join(__dirname, '..', 'tests', testFile);

    if (!fs.existsSync(testPath)) {
      log(`  WARNING: Test file not found: ${testFile}`, colors.yellow);
      resolve({ success: false, skipped: true, error: 'File not found' });
      return;
    }

    const startTime = Date.now();

    const proc = spawn('node', [testPath], {
      stdio: flags.verbose ? 'inherit' : 'pipe',
      env: { ...process.env, ...env },
      timeout: timeout,
    });

    let stdout = '';
    let stderr = '';

    if (!flags.verbose) {
      proc.stdout?.on('data', (data) => { stdout += data; });
      proc.stderr?.on('data', (data) => { stderr += data; });
    }

    const timeoutId = setTimeout(() => {
      proc.kill('SIGTERM');
      resolve({ success: false, timedOut: true, duration: Date.now() - startTime });
    }, timeout);

    proc.on('close', (code) => {
      clearTimeout(timeoutId);
      const duration = Date.now() - startTime;
      resolve({
        success: code === 0,
        code,
        duration,
        stdout,
        stderr,
      });
    });

    proc.on('error', (error) => {
      clearTimeout(timeoutId);
      resolve({
        success: false,
        error: error.message,
        duration: Date.now() - startTime,
      });
    });
  });
}

async function runSuite(suiteName) {
  const suite = config.suites[suiteName];

  if (!suite) {
    log(`ERROR: Unknown test suite: ${suiteName}`, colors.red);
    log('\nAvailable suites: ' + Object.keys(config.suites).join(', '));
    process.exit(1);
  }

  log(`\n${'='.repeat(60)}`, colors.bright);
  log(`Running ${suiteName} test suite`, colors.cyan);
  log(`${suite.description}`);
  log(`Tests: ${suite.tests.length} | Timeout: ${suite.timeout / 1000}s | Required: ${suite.required}`);
  log('='.repeat(60));

  if (flags.dryRun) {
    log('\n[DRY RUN] Would run the following tests:', colors.yellow);
    suite.tests.forEach(t => log(`  - ${t}`));
    return { passed: suite.tests.length, failed: 0, skipped: 0 };
  }

  const results = {
    passed: 0,
    failed: 0,
    skipped: 0,
    timedOut: 0,
    tests: [],
  };

  const startTime = Date.now();

  for (const testFile of suite.tests) {
    log(`\n--- ${testFile} ---`, colors.bright);

    const result = await runTest(testFile, suite.timeout, suite.env || {});

    if (result.skipped) {
      log(`  SKIPPED: ${result.error}`, colors.yellow);
      results.skipped++;
    } else if (result.timedOut) {
      log(`  TIMEOUT after ${result.duration / 1000}s`, colors.red);
      results.timedOut++;
      results.failed++;
    } else if (result.success) {
      log(`  PASSED (${(result.duration / 1000).toFixed(2)}s)`, colors.green);
      results.passed++;
    } else {
      log(`  FAILED (code: ${result.code}, ${(result.duration / 1000).toFixed(2)}s)`, colors.red);
      results.failed++;

      if (!flags.verbose && result.stderr) {
        log(`  Error output: ${result.stderr.slice(0, 500)}`, colors.dim);
      }

      if (suite.required && !flags.continue) {
        log('\nStopping: Required test failed', colors.red);
        break;
      }
    }

    results.tests.push({
      name: testFile,
      ...result,
    });
  }

  const totalDuration = Date.now() - startTime;

  // Print summary
  log(`\n${'='.repeat(60)}`, colors.bright);
  log(`Test Suite: ${suiteName}`, colors.cyan);
  log(`Duration: ${(totalDuration / 1000).toFixed(2)}s`);
  log(`Passed:  ${results.passed}`, colors.green);
  log(`Failed:  ${results.failed}`, results.failed > 0 ? colors.red : colors.reset);
  log(`Skipped: ${results.skipped}`, results.skipped > 0 ? colors.yellow : colors.reset);
  if (results.timedOut > 0) {
    log(`Timed out: ${results.timedOut}`, colors.red);
  }
  log('='.repeat(60));

  return results;
}

async function main() {
  log(`\nOwl Browser CI Test Runner`, colors.bright);
  log(`Node.js ${process.version}`);

  if (flags.list) {
    listSuites();
    process.exit(0);
  }

  const results = await runSuite(suiteName);

  // Determine exit code
  const suite = config.suites[suiteName];
  const shouldFail = suite?.required && results.failed > 0;

  if (shouldFail) {
    log(`\nCI FAILED: ${results.failed} required test(s) failed`, colors.red);
    process.exit(1);
  } else if (results.failed > 0) {
    log(`\nCI PASSED with warnings: ${results.failed} optional test(s) failed`, colors.yellow);
    process.exit(0);
  } else {
    log(`\nCI PASSED: All tests successful!`, colors.green);
    process.exit(0);
  }
}

main().catch((error) => {
  console.error('Fatal error:', error);
  process.exit(1);
});
