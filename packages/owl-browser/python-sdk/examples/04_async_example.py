#!/usr/bin/env python3
"""
Async Example

Demonstrates async/await usage with AsyncBrowser.
"""

import sys
import os
import asyncio

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import AsyncBrowser


async def scrape_url(browser: AsyncBrowser, url: str) -> dict:
    """Scrape a single URL asynchronously."""
    page = await browser.new_page()
    try:
        await page.goto(url)
        title = await page.get_title()
        text = await page.extract_text()
        return {
            "url": url,
            "title": title,
            "text_preview": text[:100].replace("\n", " "),
            "success": True,
        }
    except Exception as e:
        return {"url": url, "error": str(e), "success": False}
    finally:
        await page.close()


async def main():
    print("=== Async Browser Example ===\n")

    # Use async context manager
    async with AsyncBrowser(verbose=False) as browser:
        # Simple navigation
        print("1. Simple async navigation:")
        page = await browser.new_page()
        await page.goto("https://example.com")

        title = await page.get_title()
        print(f"   Title: {title}")

        text = await page.extract_text()
        print(f"   Text preview: {text[:80]}...")

        await page.screenshot("async_screenshot.png")
        print(f"   Screenshot saved: async_screenshot.png")

        await page.close()

        # Concurrent async scraping
        print("\n2. Concurrent async scraping:")
        urls = [
            "https://example.com",
            "https://httpbin.org/html",
            "https://httpbin.org/robots.txt",
        ]

        # Use asyncio.gather for concurrent execution
        results = await asyncio.gather(*[
            scrape_url(browser, url) for url in urls
        ])

        for result in results:
            if result["success"]:
                print(f"   {result['url']}")
                print(f"     Title: {result['title']}")
                print(f"     Preview: {result['text_preview']}...")
            else:
                print(f"   {result['url']} - Error: {result['error']}")

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    asyncio.run(main())
