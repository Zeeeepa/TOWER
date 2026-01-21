#!/usr/bin/env python3
"""
Cookies and Storage Example

Demonstrates cookie management capabilities.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser, CookieSameSite


def main():
    print("=== Cookies and Storage Example ===\n")

    with Browser(verbose=False) as browser:
        page = browser.new_page()

        # Navigate to a site
        print("Navigating to httpbin.org...")
        page.goto("https://httpbin.org/cookies")
        page.wait(1000)

        # Get initial cookies
        print("\n1. Initial cookies:")
        cookies = page.get_cookies()
        if cookies:
            for cookie in cookies:
                print(f"   {cookie.name}: {cookie.value}")
        else:
            print("   No cookies set")

        # Set some cookies
        print("\n2. Setting cookies...")
        page.set_cookie(
            url="https://httpbin.org",
            name="test_session",
            value="abc123xyz",
            path="/",
            secure=True,
        )
        print("   Set: test_session=abc123xyz")

        page.set_cookie(
            url="https://httpbin.org",
            name="user_preference",
            value="dark_mode",
            path="/",
            same_site=CookieSameSite.LAX,
            expires=int(time.time()) + 86400,  # 1 day from now
        )
        print("   Set: user_preference=dark_mode (expires in 1 day)")

        page.set_cookie(
            url="https://httpbin.org",
            name="tracking_id",
            value="track_12345",
            http_only=True,
        )
        print("   Set: tracking_id=track_12345 (httpOnly)")

        # Get cookies again
        print("\n3. Cookies after setting:")
        cookies = page.get_cookies("https://httpbin.org")
        for cookie in cookies:
            print(f"   {cookie.name}: {cookie.value}")
            print(f"     domain={cookie.domain}, path={cookie.path}")
            print(f"     secure={cookie.secure}, httpOnly={cookie.http_only}")
            print(f"     sameSite={cookie.same_site.value}, expires={cookie.expires}")

        # Delete a specific cookie
        print("\n4. Deleting 'tracking_id' cookie...")
        page.delete_cookies("https://httpbin.org", "tracking_id")

        # Verify deletion
        print("\n5. Cookies after deletion:")
        cookies = page.get_cookies("https://httpbin.org")
        for cookie in cookies:
            print(f"   {cookie.name}: {cookie.value}")

        # Delete all cookies
        print("\n6. Deleting all cookies...")
        page.delete_cookies()

        cookies = page.get_cookies()
        print(f"   Remaining cookies: {len(cookies)}")

        page.close()

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
