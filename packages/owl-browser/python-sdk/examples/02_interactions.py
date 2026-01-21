#!/usr/bin/env python3
"""
Interactions Example

Demonstrates clicking, typing, and form interactions.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser, KeyName


def main():
    print("=== Interactions Example ===\n")

    with Browser(verbose=True) as browser:
        page = browser.new_page()

        # Navigate to a search engine
        print("Navigating to Google...")
        page.goto("https://www.google.com")

        # Wait for the page to load
        page.wait(1000)

        # Take screenshot before interaction
        page.screenshot("google_before.png")
        print("Screenshot saved: google_before.png")

        # Type in the search box using natural language selector
        print("\nTyping search query...")
        try:
            # Try natural language selector first
            page.type("search box", "Owl Browser automation")
        except Exception:
            # Fallback to CSS selector
            page.type("textarea[name='q']", "Owl Browser automation")

        page.wait(500)

        # Take screenshot after typing
        page.screenshot("google_after_type.png")
        print("Screenshot saved: google_after_type.png")

        # Press Enter to search
        print("Pressing Enter to search...")
        page.press_key(KeyName.ENTER)

        # Wait for results
        page.wait(2000)

        # Take screenshot of results
        page.screenshot("google_results.png")
        print("Screenshot saved: google_results.png")

        # Extract some text from the results
        text = page.extract_text()
        print(f"\nSearch results text (first 500 chars):\n{text[:500]}...")

        page.close()

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
