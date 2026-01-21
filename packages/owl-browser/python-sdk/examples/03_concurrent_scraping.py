#!/usr/bin/env python3
"""
Concurrent Scraping Example

Demonstrates thread-safe concurrent scraping with multiple pages.
"""

import sys
import os
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser


# URLs to scrape
URLS = [
    "https://example.com",
    "https://httpbin.org/html",
    "https://httpbin.org/robots.txt",
]


def scrape_url(browser: Browser, url: str) -> dict:
    """
    Scrape a single URL.

    Each call creates its own isolated page for thread safety.
    """
    start_time = time.time()
    page = browser.new_page()

    try:
        page.goto(url)

        title = page.get_title()
        text = page.extract_text()

        elapsed = time.time() - start_time

        return {
            "url": url,
            "title": title,
            "text_length": len(text),
            "text_preview": text[:100].replace("\n", " "),
            "elapsed_seconds": round(elapsed, 2),
            "success": True,
        }
    except Exception as e:
        return {
            "url": url,
            "error": str(e),
            "success": False,
        }
    finally:
        page.close()


def main():
    print("=== Concurrent Scraping Example ===\n")

    # Create browser instance (shared across threads)
    browser = Browser(verbose=False)
    browser.launch()

    print(f"Scraping {len(URLS)} URLs concurrently...\n")

    start_time = time.time()
    results = []

    # Use ThreadPoolExecutor for concurrent scraping
    with ThreadPoolExecutor(max_workers=3) as executor:
        # Submit all tasks
        future_to_url = {
            executor.submit(scrape_url, browser, url): url
            for url in URLS
        }

        # Collect results as they complete
        for future in as_completed(future_to_url):
            result = future.result()
            results.append(result)

            if result["success"]:
                print(f"  {result['url']}")
                print(f"    Title: {result['title']}")
                print(f"    Text length: {result['text_length']} chars")
                print(f"    Preview: {result['text_preview']}...")
                print(f"    Time: {result['elapsed_seconds']}s")
            else:
                print(f"  {result['url']}")
                print(f"    Error: {result['error']}")
            print()

    total_time = time.time() - start_time

    # Summary
    successful = sum(1 for r in results if r["success"])
    print(f"=== Summary ===")
    print(f"Total URLs: {len(URLS)}")
    print(f"Successful: {successful}")
    print(f"Failed: {len(URLS) - successful}")
    print(f"Total time: {total_time:.2f}s")

    browser.close()
    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
