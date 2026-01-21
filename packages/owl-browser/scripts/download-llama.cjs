#!/usr/bin/env node

/**
 * Download llama.cpp prebuilt binaries for the current platform
 *
 * This script downloads the appropriate llama.cpp release from GitHub
 * and extracts it to third_party/llama.cpp/
 *
 * Supported platforms:
 * - macOS ARM64 (Apple Silicon)
 * - macOS x64 (Intel)
 * - Linux x64 (Ubuntu 22.04+)
 * - Linux ARM64 (Ubuntu 22.04+)
 * - Windows x64
 */

const https = require('https');
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const LLAMA_CPP_VERSION = 'latest'; // Use latest release
const THIRD_PARTY_DIR = path.join(__dirname, '../third_party');

// Determine platform-specific directory
function getLlamaPlatformDir() {
  const platform = process.platform;
  if (platform === 'darwin') {
    return path.join(THIRD_PARTY_DIR, 'llama_macos');
  } else if (platform === 'linux') {
    return path.join(THIRD_PARTY_DIR, 'llama_linux');
  } else if (platform === 'win32') {
    return path.join(THIRD_PARTY_DIR, 'llama_windows');
  } else {
    throw new Error(`Unsupported platform: ${platform}`);
  }
}

// Detect platform and architecture
function getPlatform() {
  const platform = process.platform;
  const arch = process.arch;

  if (platform === 'darwin' && arch === 'arm64') {
    return {
      name: 'macOS ARM64',
      file: 'llama-{VERSION}-bin-macos-arm64.zip',
      url: 'https://github.com/ggml-org/llama.cpp/releases/latest/download/llama-{VERSION}-bin-macos-arm64.zip'
    };
  } else if (platform === 'darwin' && arch === 'x64') {
    return {
      name: 'macOS x64',
      file: 'llama-{VERSION}-bin-macos-x64.zip',
      url: 'https://github.com/ggml-org/llama.cpp/releases/latest/download/llama-{VERSION}-bin-macos-x64.zip'
    };
  } else if (platform === 'linux' && arch === 'x64') {
    return {
      name: 'Linux x64',
      file: 'llama-{VERSION}-bin-ubuntu-x64.zip',
      url: 'https://github.com/ggml-org/llama.cpp/releases/latest/download/llama-{VERSION}-bin-ubuntu-x64.zip'
    };
  } else if (platform === 'linux' && arch === 'arm64') {
    return {
      name: 'Linux ARM64',
      file: 'llama-{VERSION}-bin-ubuntu-arm64.zip',
      url: 'https://github.com/ggml-org/llama.cpp/releases/latest/download/llama-{VERSION}-bin-ubuntu-arm64.zip'
    };
  } else if (platform === 'win32' && arch === 'x64') {
    return {
      name: 'Windows x64',
      file: 'llama-{VERSION}-bin-win-avx2-x64.zip',
      url: 'https://github.com/ggml-org/llama.cpp/releases/latest/download/llama-{VERSION}-bin-win-avx2-x64.zip'
    };
  } else {
    throw new Error(`Unsupported platform: ${platform} ${arch}`);
  }
}

// Get latest release version from GitHub API
async function getLatestVersion() {
  return new Promise((resolve, reject) => {
    const options = {
      hostname: 'api.github.com',
      path: '/repos/ggml-org/llama.cpp/releases/latest',
      headers: {
        'User-Agent': 'owl-browser-download-script'
      }
    };

    https.get(options, (res) => {
      let data = '';

      res.on('data', (chunk) => {
        data += chunk;
      });

      res.on('end', () => {
        try {
          const release = JSON.parse(data);
          // Version format: "b1234" or "v1.2.3"
          const version = release.tag_name.replace('v', '').replace('b', 'b');
          resolve(version);
        } catch (e) {
          reject(e);
        }
      });
    }).on('error', reject);
  });
}

// Download file with progress
function downloadFile(url, dest) {
  return new Promise((resolve, reject) => {
    console.log(`Downloading: ${url}`);

    const file = fs.createWriteStream(dest);

    https.get(url, (response) => {
      if (response.statusCode === 302 || response.statusCode === 301) {
        // Follow redirect
        return downloadFile(response.headers.location, dest).then(resolve).catch(reject);
      }

      if (response.statusCode !== 200) {
        reject(new Error(`Failed to download: HTTP ${response.statusCode}`));
        return;
      }

      const totalBytes = parseInt(response.headers['content-length'], 10);
      let downloadedBytes = 0;

      response.on('data', (chunk) => {
        downloadedBytes += chunk.length;
        const progress = ((downloadedBytes / totalBytes) * 100).toFixed(1);
        process.stdout.write(`\rProgress: ${progress}% (${(downloadedBytes / 1024 / 1024).toFixed(1)} MB / ${(totalBytes / 1024 / 1024).toFixed(1)} MB)`);
      });

      response.pipe(file);

      file.on('finish', () => {
        file.close();
        console.log('\n✓ Download complete');
        resolve();
      });
    }).on('error', (err) => {
      fs.unlink(dest, () => {});
      reject(err);
    });
  });
}

// Extract archive
function extractArchive(archivePath, destDir) {
  console.log('Extracting archive...');

  const platform = process.platform;

  try {
    if (platform === 'win32') {
      // Windows: Use PowerShell to extract
      execSync(`powershell -command "Expand-Archive -Path '${archivePath}' -DestinationPath '${destDir}' -Force"`, {
        stdio: 'inherit'
      });
    } else {
      // macOS/Linux: Use unzip
      execSync(`unzip -q -o "${archivePath}" -d "${destDir}"`, {
        stdio: 'inherit'
      });
    }

    console.log('✓ Extraction complete');
  } catch (e) {
    throw new Error('Failed to extract archive: ' + e.message);
  }
}

// Main download function
async function downloadLlamaCpp() {
  console.log('='.repeat(60));
  console.log('Llama.cpp Download Script');
  console.log('='.repeat(60));

  try {
    // Create third_party directory if it doesn't exist
    if (!fs.existsSync(THIRD_PARTY_DIR)) {
      fs.mkdirSync(THIRD_PARTY_DIR, { recursive: true });
      console.log('✓ Created third_party directory');
    }

    // Get platform-specific directory
    const LLAMA_DIR = getLlamaPlatformDir();
    const platformName = process.platform === 'darwin' ? 'macOS' : process.platform === 'linux' ? 'Linux' : 'Windows';

    // Detect platform
    const platformInfo = getPlatform();
    console.log(`Platform: ${platformInfo.name}`);
    console.log(`Target directory: ${LLAMA_DIR}`);

    // Get latest version
    console.log('Fetching latest release...');
    const version = await getLatestVersion();
    console.log(`Latest version: ${version}`);

    // Replace version in URL and filename
    const downloadUrl = platformInfo.url.replace(/{VERSION}/g, version);
    const fileName = platformInfo.file.replace(/{VERSION}/g, version);
    const downloadPath = path.join(THIRD_PARTY_DIR, fileName);

    // Calculate extracted directory name BEFORE using it
    const extractedDirName = fileName.replace('.zip', '');

    // Check if already downloaded
    if (fs.existsSync(LLAMA_DIR)) {
      console.log(`\n⚠️  llama.cpp already exists for ${platformName} in third_party/`);
      console.log(`Remove it to re-download: rm -rf ${path.basename(LLAMA_DIR)}`);
      process.exit(0);
    }

    // Download
    await downloadFile(downloadUrl, downloadPath);

    // Extract
    extractArchive(downloadPath, THIRD_PARTY_DIR);

    // Find the extracted directory (it might be named 'build' or similar)
    const possibleDirs = ['build', 'llama.cpp', extractedDirName];
    let extractedPath = null;

    for (const dir of possibleDirs) {
      const testPath = path.join(THIRD_PARTY_DIR, dir);
      if (fs.existsSync(testPath) && fs.existsSync(path.join(testPath, 'bin'))) {
        extractedPath = testPath;
        break;
      }
    }

    // Also check if extraction created a folder with version name
    if (!extractedPath) {
      const testPath = path.join(THIRD_PARTY_DIR, extractedDirName);
      if (fs.existsSync(testPath)) {
        extractedPath = testPath;
      }
    }

    if (extractedPath && extractedPath !== LLAMA_DIR) {
      // Remove old llama.cpp if exists
      if (fs.existsSync(LLAMA_DIR)) {
        execSync(`rm -rf "${LLAMA_DIR}"`, { stdio: 'inherit' });
      }

      fs.renameSync(extractedPath, LLAMA_DIR);
      console.log('✓ Renamed to llama.cpp');
    } else if (extractedPath === LLAMA_DIR) {
      console.log('✓ Already named llama.cpp');
    } else {
      console.log('⚠️  Could not find extracted directory');
    }

    // Clean up archive
    fs.unlinkSync(downloadPath);
    console.log('✓ Cleaned up archive');

    // Make binaries executable (Unix-like systems)
    if (process.platform !== 'win32') {
      const binDir = path.join(LLAMA_DIR, 'bin');
      if (fs.existsSync(binDir)) {
        execSync(`chmod +x ${binDir}/*`, { stdio: 'inherit' });
        console.log('✓ Made binaries executable');
      }
    }

    console.log('\n' + '='.repeat(60));
    console.log('✅ llama.cpp downloaded successfully!');
    console.log('='.repeat(60));
    console.log(`Location: ${LLAMA_DIR}`);
    console.log('\nBinaries available:');
    console.log('  - llama-server (inference server)');
    console.log('  - llama-cli (command-line interface)');
    console.log('  - llama-quantize (model quantization)');
    console.log('\nNext steps:');
    console.log('  1. Place your GGUF model in: models/llm-assist.gguf');
    console.log('  2. Run: npm run build');
    console.log('='.repeat(60));

  } catch (error) {
    console.error('\n❌ Error:', error.message);
    process.exit(1);
  }
}

// Run if executed directly
if (require.main === module) {
  downloadLlamaCpp();
}

module.exports = { downloadLlamaCpp };
