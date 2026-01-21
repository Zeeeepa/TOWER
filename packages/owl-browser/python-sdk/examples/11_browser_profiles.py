#!/usr/bin/env python3
"""
Browser Profiles Example

Demonstrates how to use browser profiles for persistent browser identity.
Profiles store:
- Browser fingerprints (user agent, canvas, WebGL, screen, timezone, etc.)
- Cookies from visited websites
- LLM and proxy configuration

This enables:
- Maintaining login sessions across browser restarts
- Consistent fingerprinting for anti-detection
- Saving and restoring complete browser state
"""

import sys
import os
import tempfile
import json

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser


def main():
    print("=== Browser Profiles Example ===\n")

    # Create a temporary profile path for this demo
    profile_path = os.path.join(tempfile.gettempdir(), "owl_demo_profile.json")
    print(f"Profile path: {profile_path}\n")

    # ==================== PART 1: Create and Save Profile ====================
    print("--- Part 1: Creating a new profile and saving cookies ---\n")

    with Browser(verbose=False) as browser:
        # Create a page with a profile path
        # If the profile doesn't exist, a new one with random fingerprints is created
        print("1. Creating page with profile...")
        page = browser.new_page(profile_path=profile_path)

        # Navigate to a site that sets cookies
        print("2. Navigating to httpbin.org to set cookies...")
        page.goto("https://httpbin.org/cookies/set?session_id=demo123&user_pref=dark")
        page.wait(1000)

        # Check the cookies we received
        print("3. Checking cookies from site:")
        cookies = page.get_cookies()
        for cookie in cookies:
            print(f"   {cookie.name}: {cookie.value} (domain: {cookie.domain})")

        # Save the profile to disk (this captures fingerprints and cookies)
        print("\n4. Saving profile to disk...")
        saved_profile = page.save_profile()
        print(f"   Saved profile: {saved_profile.profile_id}")
        print(f"   Modified at: {saved_profile.modified_at}")

        if saved_profile.fingerprint:
            print(f"   User Agent: {saved_profile.fingerprint.user_agent[:60]}...")
            print(f"   Platform: {saved_profile.fingerprint.platform}")
            print(f"   Screen: {saved_profile.fingerprint.screen_width}x{saved_profile.fingerprint.screen_height}")
            print(f"   Timezone: {saved_profile.fingerprint.timezone}")
            print(f"   Canvas Noise Seed: {saved_profile.fingerprint.canvas_noise_seed}")
            print(f"   WebGL Vendor: {saved_profile.fingerprint.webgl_vendor}")
            print(f"   WebGL Renderer: {saved_profile.fingerprint.webgl_renderer}")

        print(f"   Cookies in profile: {len(saved_profile.cookies)}")

        page.close()

    # Verify the profile file was created
    if os.path.exists(profile_path):
        file_size = os.path.getsize(profile_path)
        print(f"\n   Profile file created: {file_size} bytes")

        # Show profile JSON structure
        with open(profile_path, "r") as f:
            profile_json = json.load(f)
            print(f"   Profile version: {profile_json.get('version')}")
            print(f"   Created at: {profile_json.get('created_at')}")

    # ==================== PART 2: Load Existing Profile ====================
    print("\n\n--- Part 2: Loading existing profile (restoring session) ---\n")

    with Browser(verbose=False) as browser:
        # Create a new page with the same profile path
        # This will load the saved fingerprints and cookies
        print("1. Creating page with existing profile...")
        page = browser.new_page(profile_path=profile_path)

        # Navigate to the same site - cookies should be restored
        print("2. Navigating to httpbin.org (cookies should be restored)...")
        page.goto("https://httpbin.org/cookies")
        page.wait(1000)

        # Check if cookies are present
        print("3. Checking restored cookies:")
        cookies = page.get_cookies()
        if cookies:
            for cookie in cookies:
                print(f"   {cookie.name}: {cookie.value}")
        else:
            print("   No cookies (they may have been session cookies)")

        # Save profile again to verify fingerprint consistency
        print("\n4. Saving profile to verify fingerprint consistency...")
        profile = page.save_profile()

        if profile.fingerprint:
            print(f"   Profile ID: {profile.profile_id}")
            print(f"   User Agent: {profile.fingerprint.user_agent[:60]}...")
            print(f"   Canvas Noise Seed: {profile.fingerprint.canvas_noise_seed}")
            print("   (These should match Part 1)")

        page.close()

    # ==================== PART 3: Update Profile Cookies ====================
    print("\n\n--- Part 3: Updating profile with new cookies ---\n")

    with Browser(verbose=False) as browser:
        print("1. Loading profile...")
        page = browser.new_page(profile_path=profile_path)

        # Set additional cookies
        print("2. Setting additional cookies...")
        page.set_cookie(
            url="https://httpbin.org",
            name="new_cookie",
            value="updated_value",
            path="/",
        )
        page.set_cookie(
            url="https://httpbin.org",
            name="timestamp",
            value="1234567890",
            path="/",
        )

        # Update just the cookies in the profile
        print("3. Updating profile cookies...")
        page.update_profile_cookies()

        # Save and verify the update
        print("4. Saving and verifying updated profile:")
        profile = page.save_profile()
        print(f"   Total cookies in profile: {len(profile.cookies)}")
        for cookie in profile.cookies:
            print(f"   - {cookie.name}: {cookie.value}")

        # Save the complete profile
        print("\n5. Saving updated profile...")
        page.save_profile()
        print("   Profile saved!")

        page.close()

    # ==================== PART 4: Profile Inspection ====================
    print("\n\n--- Part 4: Inspecting profile file ---\n")

    if os.path.exists(profile_path):
        with open(profile_path, "r") as f:
            profile_data = json.load(f)

        print("Profile structure:")
        print(f"  - profile_id: {profile_data.get('profile_id', 'N/A')}")
        print(f"  - profile_name: {profile_data.get('profile_name', 'N/A')}")
        print(f"  - version: {profile_data.get('version', 'N/A')}")
        print(f"  - created_at: {profile_data.get('created_at', 'N/A')}")
        print(f"  - modified_at: {profile_data.get('modified_at', 'N/A')}")

        fp = profile_data.get("fingerprint", {})
        if fp:
            print("\n  Fingerprint:")
            print(f"    - user_agent: {fp.get('user_agent', 'N/A')[:50]}...")
            print(f"    - platform: {fp.get('platform', 'N/A')}")
            print(f"    - screen: {fp.get('screen_width', 'N/A')}x{fp.get('screen_height', 'N/A')}")
            print(f"    - timezone: {fp.get('timezone', 'N/A')}")
            print(f"    - locale: {fp.get('locale', 'N/A')}")
            print(f"    - canvas_noise_seed: {fp.get('canvas_noise_seed', 'N/A')}")
            print(f"    - webgl_vendor: {fp.get('webgl_vendor', 'N/A')}")
            print(f"    - webgl_renderer: {fp.get('webgl_renderer', 'N/A')}")

        cookies = profile_data.get("cookies", [])
        print(f"\n  Cookies ({len(cookies)}):")
        for c in cookies[:5]:  # Show first 5
            print(f"    - {c.get('name')}: {c.get('value')} ({c.get('domain')})")
        if len(cookies) > 5:
            print(f"    ... and {len(cookies) - 5} more")

    # Cleanup
    print("\n\n--- Cleanup ---")
    if os.path.exists(profile_path):
        os.remove(profile_path)
        print(f"Removed demo profile: {profile_path}")

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
