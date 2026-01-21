#!/usr/bin/env python3
"""
Video Recording Example

Demonstrates video recording capabilities.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser


def main():
    print("=== Video Recording Example ===\n")

    with Browser(verbose=True) as browser:
        page = browser.new_page()

        # Start recording
        print("Starting video recording at 30 FPS...")
        page.start_video_recording(fps=30)

        # Perform some actions
        print("\nPerforming actions...")

        # Navigate to a few pages
        print("  Navigating to example.com...")
        page.goto("https://example.com")
        page.wait(1500)

        print("  Scrolling down...")
        page.scroll_by(0, 200)
        page.wait(500)

        print("  Navigating to httpbin.org...")
        page.goto("https://httpbin.org")
        page.wait(1500)

        print("  Scrolling down...")
        page.scroll_by(0, 300)
        page.wait(500)

        # Get recording stats
        print("\nGetting recording stats...")
        try:
            stats = page.get_video_stats()
            print(f"  Stats: {stats}")
        except Exception as e:
            print(f"  Could not get stats: {e}")

        # Stop recording
        print("\nStopping video recording...")
        try:
            video_path = page.stop_video_recording()
            print(f"Video saved to: {video_path}")
        except Exception as e:
            print(f"Could not stop recording: {e}")

        page.close()

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
