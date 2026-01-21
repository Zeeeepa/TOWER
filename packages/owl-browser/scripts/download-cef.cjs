#!/usr/bin/env node

/**
 * Script to download CEF binary distribution
 * Downloads the appropriate CEF build for the current platform
 */

const https = require('https');
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const os = require('os');

// CEF version and build configuration
const CEF_VERSION = '143.0.10+g8aed01b+chromium-143.0.7499.110';
const CEF_BASE_URL = 'https://cef-builds.spotifycdn.com';

// Determine platform
function getPlatform() {
  const platform = os.platform();
  const arch = os.arch();

  if (platform === 'darwin') {
    return arch === 'arm64' ? 'macosarm64' : 'macosx64';
  } else if (platform === 'linux') {
    return arch === 'arm64' ? 'linuxarm64' : 'linux64';
  } else if (platform === 'win32') {
    return arch === 'x64' ? 'windows64' : 'windows32';
  }

  throw new Error(`Unsupported platform: ${platform} ${arch}`);
}

function getCEFFileName() {
  const platform = getPlatform();

  // Format: cef_binary_{version}_{platform} (no _standard suffix for newer versions)
  return `cef_binary_${CEF_VERSION}_${platform}`;
}

function downloadFile(url, dest) {
  return new Promise((resolve, reject) => {
    console.log(`Downloading: ${url}`);

    const file = fs.createWriteStream(dest);

    https.get(url, (response) => {
      if (response.statusCode === 302 || response.statusCode === 301) {
        // Handle redirect
        downloadFile(response.headers.location, dest)
          .then(resolve)
          .catch(reject);
        return;
      }

      if (response.statusCode !== 200) {
        reject(new Error(`Failed to download: ${response.statusCode}`));
        return;
      }

      const totalSize = parseInt(response.headers['content-length'], 10);
      let downloaded = 0;

      response.on('data', (chunk) => {
        downloaded += chunk.length;
        const percent = ((downloaded / totalSize) * 100).toFixed(1);
        process.stdout.write(`\rProgress: ${percent}%`);
      });

      response.pipe(file);

      file.on('finish', () => {
        file.close();
        console.log('\nDownload complete');
        resolve();
      });
    }).on('error', (err) => {
      fs.unlink(dest, () => {});
      reject(err);
    });
  });
}

async function extractArchive(archivePath, destDir) {
  console.log(`Extracting ${archivePath}...`);

  const ext = path.extname(archivePath);

  if (ext === '.bz2' || archivePath.endsWith('.tar.bz2')) {
    execSync(`tar -xjf "${archivePath}" -C "${destDir}"`, { stdio: 'inherit' });
  } else if (ext === '.zip') {
    execSync(`unzip -q "${archivePath}" -d "${destDir}"`, { stdio: 'inherit' });
  } else {
    throw new Error(`Unsupported archive format: ${ext}`);
  }

  console.log('Extraction complete');
}

async function main() {
  const thirdPartyDir = path.join(__dirname, '..', 'third_party');

  // Determine platform-specific directory
  const platform = os.platform();
  let platformDir;
  if (platform === 'darwin') {
    platformDir = 'cef_macos';
  } else if (platform === 'linux') {
    platformDir = 'cef_linux';
  } else if (platform === 'win32') {
    platformDir = 'cef_windows';
  } else {
    throw new Error(`Unsupported platform: ${platform}`);
  }

  const cefPlatformDir = path.join(thirdPartyDir, platformDir);
  const cefFileName = getCEFFileName();
  const archiveExt = platform === 'win32' ? '.zip' : '.tar.bz2';
  const archiveName = `${cefFileName}${archiveExt}`;
  const archivePath = path.join(thirdPartyDir, archiveName);
  const extractedDir = path.join(thirdPartyDir, cefFileName);

  // Create third_party directory
  if (!fs.existsSync(thirdPartyDir)) {
    fs.mkdirSync(thirdPartyDir, { recursive: true });
  }

  // Check if already downloaded
  if (fs.existsSync(cefPlatformDir)) {
    console.log(`CEF already downloaded for ${platform}:`, cefPlatformDir);
    return;
  }

  // Download URL
  const downloadUrl = `${CEF_BASE_URL}/${archiveName}`;

  try {
    // Download
    await downloadFile(downloadUrl, archivePath);

    // Extract
    await extractArchive(archivePath, thirdPartyDir);

    // Move extracted directory to platform-specific location
    if (fs.existsSync(extractedDir)) {
      fs.renameSync(extractedDir, cefPlatformDir);
      console.log(`Moved CEF to platform directory: ${cefPlatformDir}`);
    } else {
      throw new Error(`Extracted directory not found: ${extractedDir}`);
    }

    // Cleanup archive
    fs.unlinkSync(archivePath);

    console.log('\nCEF setup complete!');
    console.log(`CEF location: ${cefPlatformDir}`);
    console.log(`Platform: ${platform}`);

  } catch (error) {
    console.error('Error downloading CEF:', error.message);
    console.error('\nManual download instructions:');
    console.error(`1. Visit: ${CEF_BASE_URL}/index.html`);
    console.error(`2. Download: ${archiveName}`);
    console.error(`3. Extract to: ${thirdPartyDir}`);
    console.error(`4. Rename extracted folder to: ${platformDir}`);
    process.exit(1);
  }
}

main();