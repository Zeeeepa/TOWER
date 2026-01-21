#!/usr/bin/env python3
"""
WebSocket Mode Example

Demonstrates connecting to a remote Owl Browser HTTP server using WebSocket
transport instead of REST API. WebSocket provides lower latency and persistent
connections for high-frequency operations.

Benefits of WebSocket over HTTP:
- Lower latency (no HTTP overhead per request)
- Persistent connection (no reconnection overhead)
- Real-time communication
- More efficient for high-frequency operations

Prerequisites:
    Start the HTTP server first (WebSocket is enabled by default on /ws route):
    ./owl_http_server --config config.yaml

    Or with environment variables:
    OWL_TOKEN=test-token-12345 ./owl_http_server
"""

import sys
import os
import time

# Add parent directory to path for local development
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser, RemoteConfig, TransportMode, ConnectionMode


def main():
    print("=== WebSocket Mode Example ===\n")

    # Configure remote connection with WebSocket transport
    remote_config = RemoteConfig(
        url="http://127.0.0.1:8080",
        token="test-token-12345",
        transport=TransportMode.WEBSOCKET,  # Use WebSocket instead of HTTP
        timeout=60000,  # 60 second timeout for slower operations
        verify_ssl=False  # Disable SSL verification for local testing
    )

    print(f"Connecting to remote server: {remote_config.url}")
    print(f"Transport: WebSocket (ws://{remote_config.url.split('://')[1]}/ws)")

    # Create browser with WebSocket configuration
    with Browser(remote=remote_config, verbose=True) as browser:
        # Verify we're in remote mode
        print(f"\nConnection mode: {browser.mode}")
        print(f"Is remote: {browser.is_remote}")
        assert browser.mode == ConnectionMode.REMOTE, "Expected REMOTE mode"
        assert browser.is_remote is True, "Expected is_remote to be True"
        print("WebSocket connection established")

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
        print("Navigation successful")

        # Take a screenshot
        print("\n--- Screenshot Test ---")
        screenshot_path = "websocket_screenshot.png"
        screenshot_data = page.screenshot(screenshot_path)
        print(f"Screenshot saved to: {screenshot_path}")
        screenshot_size = len(screenshot_data) if screenshot_data else 0
        print(f"Screenshot size: {screenshot_size} bytes")
        if screenshot_size > 0:
            print("Screenshot captured")
        else:
            print("Note: Screenshot returned empty (may be a display issue)")

        # Extract text content
        print("\n--- Text Extraction Test ---")
        text = page.extract_text()
        if text:
            print(f"Extracted text (first 200 chars):\n{text[:200]}...")
            print("Text extraction successful")
        else:
            print("Note: Text extraction returned empty (page may still be loading)")

        # Test rapid commands (benefits of WebSocket low latency)
        print("\n--- Rapid Commands Test (WebSocket Advantage) ---")
        start_time = time.time()

        for i in range(5):
            # Quick scroll operations
            page.scroll_by(0, 50)
            page.scroll_by(0, -50)

        elapsed = time.time() - start_time
        print(f"10 scroll operations completed in {elapsed:.3f}s")
        print(f"Average latency: {(elapsed / 10) * 1000:.1f}ms per operation")
        print("Rapid commands test completed")

        # Get page as markdown
        print("\n--- Markdown Extraction Test ---")
        markdown = page.get_markdown(include_links=True)
        if markdown:
            print(f"Markdown (first 200 chars):\n{markdown[:200]}...")
            print("Markdown extraction successful")
        else:
            print("Note: Markdown extraction returned empty")

        # Get demographics info
        print("\n--- Demographics Test ---")
        try:
            demographics = browser.get_demographics()
            print(f"Demographics: {demographics}")
            print("Demographics retrieved")
        except Exception as e:
            print(f"Note: Demographics not available ({e})")

        # Close the page
        print("\n--- Cleanup ---")
        page.close()
        print("Page closed")

    print("\n" + "=" * 50)
    print("=== WebSocket Mode Test Complete! ===")
    print("=" * 50)


def test_latency_comparison():
    """Compare latency between HTTP and WebSocket modes."""
    print("\n=== Latency Comparison: HTTP vs WebSocket ===\n")

    # Number of operations to test
    num_ops = 10

    # Test HTTP mode
    print("Testing HTTP mode...")
    http_config = RemoteConfig(
        url="http://127.0.0.1:8080",
        token="test-token-12345",
        transport=TransportMode.HTTP,
        timeout=60000
    )

    http_times = []
    with Browser(remote=http_config) as browser:
        page = browser.new_page()
        page.goto("https://example.com")
        time.sleep(1)

        for _ in range(num_ops):
            start = time.time()
            page.get_page_info()
            http_times.append(time.time() - start)

        page.close()

    http_avg = sum(http_times) / len(http_times) * 1000

    # Test WebSocket mode
    print("Testing WebSocket mode...")
    ws_config = RemoteConfig(
        url="http://127.0.0.1:8080",
        token="test-token-12345",
        transport=TransportMode.WEBSOCKET,
        timeout=60000
    )

    ws_times = []
    with Browser(remote=ws_config) as browser:
        page = browser.new_page()
        page.goto("https://example.com")
        time.sleep(1)

        for _ in range(num_ops):
            start = time.time()
            page.get_page_info()
            ws_times.append(time.time() - start)

        page.close()

    ws_avg = sum(ws_times) / len(ws_times) * 1000

    # Results
    print(f"\n{'='*50}")
    print(f"Results ({num_ops} operations each):")
    print(f"  HTTP average:      {http_avg:.1f}ms")
    print(f"  WebSocket average: {ws_avg:.1f}ms")
    print(f"  Improvement:       {((http_avg - ws_avg) / http_avg * 100):.1f}%")
    print(f"{'='*50}")


def test_reconnection():
    """Test WebSocket reconnection behavior."""
    print("\n=== WebSocket Connection Test ===\n")

    ws_config = RemoteConfig(
        url="http://127.0.0.1:8080",
        token="test-token-12345",
        transport=TransportMode.WEBSOCKET,
        timeout=10000
    )

    print("Creating WebSocket connection...")
    browser = Browser(remote=ws_config, verbose=True)

    try:
        browser.launch()
        print("Connection established")

        # Create a page and do some work
        page = browser.new_page()
        print(f"Created page: {page.id}")

        page.goto("https://example.com")
        print("Navigation complete")

        # Check running status
        print(f"Browser running: {browser.is_running()}")

        page.close()
        print("Page closed")

    finally:
        browser.close()
        print("Browser closed")

    print("\nWebSocket connection test passed")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="WebSocket Mode Example")
    parser.add_argument("--compare", action="store_true", help="Run latency comparison test")
    parser.add_argument("--reconnect", action="store_true", help="Run reconnection test")
    parser.add_argument("--all", action="store_true", help="Run all tests")
    args = parser.parse_args()

    # Always run main test
    main()

    if args.compare or args.all:
        test_latency_comparison()

    if args.reconnect or args.all:
        test_reconnection()
