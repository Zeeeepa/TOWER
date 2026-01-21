#!/usr/bin/env python3
"""
AI Features Example

Demonstrates LLM-powered features like page querying and natural language automation.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser, LLMStatus


def main():
    print("=== AI Features Example ===\n")

    with Browser(verbose=True) as browser:
        # Check LLM status
        print("Checking LLM status...")
        status = browser.get_llm_status()
        print(f"LLM Status: {status}")

        if status == LLMStatus.UNAVAILABLE:
            print("Warning: LLM is not available. Some features may not work.")

        page = browser.new_page()

        # Navigate to a news site or content page
        print("\nNavigating to Wikipedia...")
        page.goto("https://en.wikipedia.org/wiki/Web_browser")

        # Wait for content to load
        page.wait(2000)

        # Get page summary using LLM
        print("\n1. Getting page summary...")
        try:
            summary = page.summarize_page()
            print(f"Summary: {summary}")
        except Exception as e:
            print(f"Summary not available: {e}")

        # Query the page
        print("\n2. Querying the page with LLM...")
        try:
            answer = page.query_page("What is the main topic of this page?")
            print(f"Q: What is the main topic of this page?")
            print(f"A: {answer}")
        except Exception as e:
            print(f"Query not available: {e}")

        # Try another query
        print("\n3. Another query...")
        try:
            answer = page.query_page("When was the first web browser created?")
            print(f"Q: When was the first web browser created?")
            print(f"A: {answer}")
        except Exception as e:
            print(f"Query not available: {e}")

        # Natural language automation (if available)
        print("\n4. Testing Natural Language Automation...")
        try:
            result = page.execute_nla("scroll down a little bit")
            print(f"NLA result: {result}")
        except Exception as e:
            print(f"NLA not available: {e}")

        # Take a screenshot
        page.screenshot("ai_features_screenshot.png")
        print("\nScreenshot saved: ai_features_screenshot.png")

        page.close()

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
