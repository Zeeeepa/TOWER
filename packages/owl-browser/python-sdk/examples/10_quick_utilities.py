#!/usr/bin/env python3
"""
Quick Utilities Example

Demonstrates the quick utility functions for simple one-off operations.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import quick_screenshot, quick_extract, quick_query


def main():
    print("=== Quick Utilities Example ===\n")

    # Quick screenshot
    print("1. Taking a quick screenshot...")
    try:
        png_data = quick_screenshot("https://example.com", "quick_screenshot.png")
        print(f"   Screenshot saved: quick_screenshot.png ({len(png_data)} bytes)")
    except Exception as e:
        print(f"   Error: {e}")

    # Quick extract
    print("\n2. Quick text extraction...")
    try:
        text = quick_extract("https://example.com", "body")
        print(f"   Extracted {len(text)} characters")
        print(f"   Preview: {text[:150]}...")
    except Exception as e:
        print(f"   Error: {e}")

    # Quick query (requires LLM)
    print("\n3. Quick LLM query...")
    try:
        answer = quick_query(
            "https://en.wikipedia.org/wiki/Python_(programming_language)",
            "What year was Python first released?"
        )
        print(f"   Q: What year was Python first released?")
        print(f"   A: {answer}")
    except Exception as e:
        print(f"   Query not available (LLM may not be ready): {e}")

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
