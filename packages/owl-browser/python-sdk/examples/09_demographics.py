#!/usr/bin/env python3
"""
Demographics Example

Demonstrates getting user demographics (location, time, weather).
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser


def main():
    print("=== Demographics Example ===\n")

    with Browser(verbose=False) as browser:
        # Get complete demographics
        print("1. Getting complete demographics...")
        try:
            demographics = browser.get_demographics()
            print(f"   Demographics: {demographics}")
        except Exception as e:
            print(f"   Error: {e}")

        # Get location
        print("\n2. Getting location...")
        try:
            location = browser.get_location()
            if location.get("success"):
                print(f"   IP: {location.get('ip')}")
                print(f"   City: {location.get('city')}")
                print(f"   Region: {location.get('region')}")
                print(f"   Country: {location.get('country')}")
                print(f"   Coordinates: {location.get('latitude')}, {location.get('longitude')}")
                print(f"   Timezone: {location.get('timezone')}")
            else:
                print(f"   Location unavailable: {location.get('error')}")
        except Exception as e:
            print(f"   Error: {e}")

        # Get datetime
        print("\n3. Getting datetime...")
        try:
            datetime_info = browser.get_datetime()
            print(f"   Date: {datetime_info.get('date')}")
            print(f"   Time: {datetime_info.get('time')}")
            print(f"   Day: {datetime_info.get('day_of_week')}")
            print(f"   Timezone: {datetime_info.get('timezone')}")
            print(f"   Unix timestamp: {datetime_info.get('unix_timestamp')}")
        except Exception as e:
            print(f"   Error: {e}")

        # Get weather
        print("\n4. Getting weather...")
        try:
            weather = browser.get_weather()
            if weather.get("success"):
                print(f"   Condition: {weather.get('condition')}")
                print(f"   Description: {weather.get('description')}")
                print(f"   Temperature: {weather.get('temperature_c')}°C / {weather.get('temperature_f')}°F")
                print(f"   Humidity: {weather.get('humidity')}%")
                print(f"   Wind: {weather.get('wind_speed_kmh')} km/h")
            else:
                print(f"   Weather unavailable: {weather.get('error')}")
        except Exception as e:
            print(f"   Error: {e}")

        # Create a page and use demographics for context-aware browsing
        print("\n5. Using demographics for location-aware search...")
        page = browser.new_page()

        try:
            location = browser.get_location()
            city = location.get("city", "Unknown")

            page.goto("https://www.google.com")
            page.wait(1000)

            # Search for local info
            search_query = f"weather in {city}"
            print(f"   Searching for: {search_query}")

            try:
                page.type("textarea[name='q']", search_query)
                page.press_key("Enter")
                page.wait(2000)
                page.screenshot("local_search.png")
                print("   Screenshot saved: local_search.png")
            except Exception:
                print("   Could not perform search")

        except Exception as e:
            print(f"   Error: {e}")

        page.close()

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
