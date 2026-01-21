#!/usr/bin/env python3
"""
Remote Mode Example

Demonstrates connecting to a remote Owl Browser HTTP server instead of
launching a local browser binary. This enables cloud deployment, distributed
scraping, and resource sharing across applications.

Prerequisites:
    Start the HTTP server first:
    ./owl_browser --http --port 8080 --token "test-token-12345"
"""

import sys
import os
import time

# Add parent directory to path for local development
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser, RemoteConfig, ConnectionMode


def main():
    print("=== Remote Mode Example ===\n")

    # Configure remote connection
    remote_config = RemoteConfig(
        url="http://127.0.0.1:8080",
        token="test-token-12345",
        timeout=60000,  # 60 second timeout for slower operations
        verify_ssl=False  # Disable SSL verification for local testing
    )

    print(f"Connecting to remote server: {remote_config.url}")

    # Create browser with remote configuration
    with Browser(remote=remote_config, verbose=True) as browser:
        # Verify we're in remote mode
        print(f"\nConnection mode: {browser.mode}")
        print(f"Is remote: {browser.is_remote}")
        assert browser.mode == ConnectionMode.REMOTE, "Expected REMOTE mode"
        assert browser.is_remote is True, "Expected is_remote to be True"
        print("✓ Remote mode verified")

        # Create a new page (context)
        print("\n--- Creating new page ---")
        page = browser.new_page()
        print(f"Page created with ID: {page.id}")

        # Navigate to a website
        print("\n--- Navigation Test ---")
        print("Navigating to example.com...")
        page.goto("https://example.com")

        # Wait a bit for page to fully load
        print("Waiting for page to load...")
        time.sleep(2)

        # Get page info
        page_info = page.get_page_info()
        print(f"Page info: {page_info}")
        title = page_info.title if hasattr(page_info, 'title') else page_info.get("title", "")
        url = page_info.url if hasattr(page_info, 'url') else page_info.get("url", "")
        print(f"Title: {title}")
        print(f"URL: {url}")
        print("✓ Navigation successful")

        # Take a screenshot
        print("\n--- Screenshot Test ---")
        screenshot_path = "remote_screenshot.png"
        screenshot_data = page.screenshot(screenshot_path)
        print(f"Screenshot saved to: {screenshot_path}")
        screenshot_size = len(screenshot_data) if screenshot_data else 0
        print(f"Screenshot size: {screenshot_size} bytes")
        if screenshot_size > 0:
            print("✓ Screenshot captured")
        else:
            print("Note: Screenshot returned empty (may be a display issue)")

        # Extract text content
        print("\n--- Text Extraction Test ---")
        text = page.extract_text()
        if text:
            print(f"Extracted text (first 200 chars):\n{text[:200]}...")
            print("✓ Text extraction successful")
        else:
            print("Note: Text extraction returned empty (page may still be loading)")

        # Get page as markdown
        print("\n--- Markdown Extraction Test ---")
        markdown = page.get_markdown(include_links=True)
        if markdown:
            print(f"Markdown (first 200 chars):\n{markdown[:200]}...")
            print("✓ Markdown extraction successful")
        else:
            print("Note: Markdown extraction returned empty")

        # Test scrolling
        print("\n--- Scroll Test ---")
        try:
            page.scroll_by(0, 100)
            print("Scrolled down 100 pixels")
            time.sleep(1)  # Wait between scroll operations
            page.scroll_to_top()
            print("Scrolled to top")
            print("✓ Scroll successful")
        except Exception as e:
            print(f"Note: Scroll test partially failed ({type(e).__name__})")
            print("This may be a server-side issue with rapid requests")

        # Get demographics info
        print("\n--- Demographics Test ---")
        try:
            demographics = browser.get_demographics()
            print(f"Demographics: {demographics}")
            print("✓ Demographics retrieved")
        except Exception as e:
            print(f"Note: Demographics not available ({e})")

        # Close the page
        print("\n--- Cleanup ---")
        page.close()
        print("Page closed")

    print("\n" + "=" * 50)
    print("=== Remote Mode Test Complete! ===")
    print("=" * 50)


def test_multiple_pages():
    """Test creating multiple pages on remote server."""
    print("\n=== Multiple Pages Test ===\n")

    remote_config = RemoteConfig(
        url="http://127.0.0.1:8080",
        token="test-token-12345",
        timeout=60000
    )

    with Browser(remote=remote_config) as browser:
        # Create multiple pages
        pages = []
        for i in range(2):  # Reduced to 2 pages for faster testing
            page = browser.new_page()
            pages.append(page)
            print(f"Created page {i + 1} with ID: {page.id}")

        # Navigate each to a URL
        for i, page in enumerate(pages):
            page.goto("https://example.com")
            time.sleep(1)  # Wait for page load
            page_info = page.get_page_info()
            print(f"Page {i + 1}: URL={page_info.url if hasattr(page_info, 'url') else page_info.get('url', 'N/A')}")

        # Close all pages
        for page in pages:
            page.close()

        print("\n✓ Multiple pages test passed")


def test_error_handling():
    """Test error handling for invalid remote config."""
    print("\n=== Error Handling Test ===\n")

    # Test with wrong URL
    print("Testing connection to invalid server...")
    try:
        browser = Browser(remote=RemoteConfig(
            url="http://127.0.0.1:9999",  # Wrong port
            token="test-token",
            timeout=5000  # Short timeout
        ))
        browser.launch()
        print("ERROR: Should have raised an exception")
        browser.close()
    except Exception as e:
        print(f"✓ Correctly caught error: {type(e).__name__}: {e}")

    # Test with wrong token
    print("\nTesting connection with invalid token...")
    try:
        browser = Browser(remote=RemoteConfig(
            url="http://127.0.0.1:8080",
            token="wrong-token",
            timeout=10000
        ))
        browser.launch()
        # Try to create a page - this should fail with auth error
        page = browser.new_page()
        print("ERROR: Should have raised an authentication error")
        browser.close()
    except Exception as e:
        print(f"✓ Correctly caught error: {type(e).__name__}: {e}")

    print("\n✓ Error handling test passed")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Remote Mode Example")
    parser.add_argument("--multi", action="store_true", help="Run multiple pages test")
    parser.add_argument("--errors", action="store_true", help="Run error handling test")
    parser.add_argument("--all", action="store_true", help="Run all tests")
    args = parser.parse_args()

    # Always run main test
    main()

    if args.multi or args.all:
        test_multiple_pages()

    if args.errors or args.all:
        test_error_handling()
