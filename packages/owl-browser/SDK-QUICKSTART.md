# Owl Browser SDK - Quick Start Guide

## Install & Run in 3 Steps

### 1. Build SDK

```bash
npm run build:sdk
```

### 2. Write Your Script

Create `test.js`:

```javascript
const { Browser } = require('./sdk/dist/index.js');

async function main() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();
  await page.goto('https://example.com');
  await page.screenshot({ path: '/tmp/screenshot.png' });

  await browser.close();
}

main();
```

### 3. Run It

```bash
node test.js
```

**That's it! 9 lines of code.**

## More Examples

### Natural Language Selectors (19 lines)

```javascript
const { Browser } = require('./sdk/dist/index.js');

async function main() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();
  await page.goto('https://google.com/ncr');

  // Natural language selectors!
  await page.type('search box', 'Owl Browser');
  await page.pressKey('Enter');
  await page.wait(2000);

  await page.screenshot({ path: '/tmp/google-search.png' });

  await browser.close();
}

main();
```

### AI-Powered Automation (24 lines)

```javascript
const { Browser } = require('./sdk/dist/index.js');

async function main() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();

  // Natural language automation - the LLM does everything!
  await page.executeNLA('go to google.com/ncr and search for banana');
  await page.screenshot({ path: '/tmp/google-banana.png' });

  await page.executeNLA('visit reddit.com');
  await page.screenshot({ path: '/tmp/reddit.png' });

  await browser.close();
}

main();
```

**Just tell it what to do in plain English!**

### Web Scraping (23 lines)

```javascript
const { Browser } = require('./sdk/dist/index.js');

async function main() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();
  await page.goto('https://news.ycombinator.com');

  const markdown = await page.getMarkdown();
  console.log(markdown.substring(0, 500) + '...\n');

  await page.screenshot({ path: '/tmp/hackernews.png' });

  await browser.close();
}

main();
```

### Video Recording (25 lines)

```javascript
const { Browser } = require('./sdk/dist/index.js');

async function main() {
  const browser = new Browser();
  await browser.launch();

  const page = await browser.newPage();

  await page.startVideoRecording();

  await page.goto('https://example.com');
  await page.scrollToBottom();
  await page.wait(2000);

  const videoPath = await page.stopVideoRecording();

  await browser.close();
  console.log('Video saved to:', videoPath);
}

main();
```

### Multiple Pages (31 lines)

```javascript
const { Browser } = require('./sdk/dist/index.js');

async function main() {
  const browser = new Browser();
  await browser.launch();

  const page1 = await browser.newPage();
  const page2 = await browser.newPage();
  const page3 = await browser.newPage();

  // Navigate all pages in parallel
  await Promise.all([
    page1.goto('https://example.com'),
    page2.goto('https://news.ycombinator.com'),
    page3.goto('https://en.wikipedia.org')
  ]);

  // Take screenshots
  await page1.screenshot({ path: '/tmp/page1.png' });
  await page2.screenshot({ path: '/tmp/page2.png' });
  await page3.screenshot({ path: '/tmp/page3.png' });

  await browser.close();
}

main();
```

## Key Features

### 1. Auto-Handles Everything

The SDK automatically:
- Waits for page loads
- Waits for LLM to be ready
- Saves screenshots to file
- Retries failed requests
- Cleans up resources

### 2. Natural Language

```javascript
// Instead of brittle CSS selectors:
await page.click('#search-btn-xyz-123');

// Use natural language:
await page.click('search button');
```

### 3. One-Liner Actions

```javascript
await page.screenshot({ path: '/tmp/out.png' });  // Auto-saves
await page.goto('https://example.com');            // Auto-waits
await page.executeNLA('search for cats');          // AI does it all
```

## Line Count Comparison

| Example | Lines | What It Does |
|---------|-------|--------------|
| `simple.js` | 19 | Navigate + screenshot |
| `web-scraping.js` | 23 | Extract as Markdown |
| `nla-google.js` | 24 | AI automation |
| `video-recording.js` | 25 | Record session |
| `google-search.js` | 25 | Natural language search |
| `multiple-pages.js` | 31 | 3 tabs in parallel |

**Average: 24 lines per example**

Compare to Puppeteer/Playwright: 50-100+ lines for the same thing!

## Available Scripts

```bash
# Build SDK
npm run build:sdk

# Run examples
node sdk/examples/simple.js
node sdk/examples/google-search.js
node sdk/examples/nla-google.js
node sdk/examples/web-scraping.js
node sdk/examples/video-recording.js
node sdk/examples/multiple-pages.js
node sdk/examples/ai-features.js

# Test SDK
npm run test:sdk
```

## Configuration

```javascript
const browser = new Browser({
  verbose: true,      // Enable logging
  initTimeout: 60000  // Increase timeout if needed
});
```

## vs Puppeteer/Playwright

| Feature | Owl SDK | Puppeteer |
|---------|---------|-----------|
| Setup | 9 lines | 30+ lines |
| Selectors | Natural language | CSS only |
| LLM | Built-in | Not available |
| Auto-wait | Yes | Manual |
| Screenshots | `{ path: '...' }` | Complex |
| Learning curve | 5 minutes | Hours |

## Documentation

- [Full SDK API](sdk/README.md)
- [Examples](sdk/examples/)
- [Main README](README.md)

## Summary

- **Simple**: Just 9-24 lines of code
- **Powerful**: AI, natural language, video recording
- **Smart**: Auto-waits, auto-retries, auto-cleanup
- **Fast**: Native C++ browser, <1s startup

**The SDK does the heavy lifting. You just write the automation!**
