#!/usr/bin/env node

/**
 * Build script to create a standalone MCP server bundle with esbuild
 * This bundles ALL dependencies including @modelcontextprotocol/sdk
 * into a single executable file that requires no npm packages.
 */

const esbuild = require('esbuild');
const fs = require('fs');
const path = require('path');

const srcDir = path.join(__dirname, '../src');
const distDir = path.join(__dirname, '../dist');
const outputFile = path.join(distDir, 'mcp-server-standalone.cjs');

console.log('Building standalone MCP server with esbuild...');

// Ensure dist directory exists
if (!fs.existsSync(distDir)) {
  fs.mkdirSync(distDir, { recursive: true });
  console.log('✓ Created dist directory');
}

// Build with esbuild
esbuild.build({
  entryPoints: [path.join(srcDir, 'mcp-server.cjs')],
  bundle: true,
  platform: 'node',
  target: 'node18',
  format: 'cjs',
  outfile: outputFile,
  external: [],  // Bundle everything, no externals
  minify: false,  // Keep readable for debugging
  sourcemap: false,
  logLevel: 'info',
}).then(() => {
  // Add shebang to the beginning of the file (only if not already present)
  const content = fs.readFileSync(outputFile, 'utf8');
  if (!content.startsWith('#!')) {
    fs.writeFileSync(outputFile, '#!/usr/bin/env node\n' + content, 'utf8');
  }
  // Make it executable
  fs.chmodSync(outputFile, 0o755);

  // Get file size
  const stats = fs.statSync(outputFile);
  const sizeKB = (stats.size / 1024).toFixed(2);

  console.log('\n✅ Build complete!');
  console.log(`✓ Bundle size: ${sizeKB} KB`);
  console.log(`✓ Output: ${outputFile}`);
  console.log('\nThe standalone bundle includes:');
  console.log('  - browser-registry.cjs');
  console.log('  - browser-wrapper.cjs');
  console.log('  - mcp-server.cjs');
  console.log('  - @modelcontextprotocol/sdk (all dependencies)');
  console.log('  - All npm packages bundled');
  console.log('\nUsers can run it with just Node.js installed:');
  console.log(`  node ${outputFile}`);
  console.log('or:');
  console.log(`  ${outputFile}`);
}).catch((error) => {
  console.error('❌ Build failed:', error);
  process.exit(1);
});