#!/usr/bin/env python3
"""
Basic Navigation Example

Demonstrates basic browser navigation and screenshot capabilities.
"""

import sys
import os

# Add parent directory to path for local development
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser


def main():
    print("=== Basic Navigation Example ===\n")

    # Create browser with context manager (auto-closes on exit)
    with Browser(verbose=False) as browser:
        # Create a new page
        page = browser.new_page()
        print("Created new page")

        # Navigate to a website
        print("\nNavigating to example.com...")
        page.goto("https://example.com")

        # Get page info
        title = page.get_title()
        url = page.get_current_url()
        print(f"Title: {title}")
        print(f"URL: {url}")

        # Take a screenshot
        screenshot_path = "example_screenshot.png"
        page.screenshot(screenshot_path)
        print(f"\nScreenshot saved to: {screenshot_path}")

        # Extract text content
        text = page.extract_text()
        print(f"\nPage text (first 200 chars):\n{text[:200]}...")

        # Get page as markdown
        markdown = page.get_markdown(include_links=True)
        print(f"\nMarkdown (first 300 chars):\n{markdown[:300]}...")

        # Close the page
        page.close()
        print("\nPage closed")

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
