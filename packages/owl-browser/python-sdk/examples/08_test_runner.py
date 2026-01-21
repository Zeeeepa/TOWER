#!/usr/bin/env python3
"""
Test Runner Example

Demonstrates running tests from JSON templates (Developer Playground format).
"""

import sys
import os
import json

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser


def main():
    print("=== Test Runner Example ===\n")

    # Define a test inline
    test_template = {
        "name": "Example.com Navigation Test",
        "description": "Tests basic navigation and content extraction on example.com",
        "steps": [
            {
                "type": "navigate",
                "url": "https://example.com",
                "selected": True
            },
            {
                "type": "wait",
                "duration": 1000,
                "selected": True
            },
            {
                "type": "screenshot",
                "filename": "test_step_1.png",
                "selected": True
            },
            {
                "type": "extract",
                "selector": "h1",
                "selected": True
            },
            {
                "type": "scroll_down",
                "selected": True
            },
            {
                "type": "wait",
                "duration": 500,
                "selected": True
            },
            {
                "type": "screenshot",
                "filename": "test_step_2.png",
                "selected": True
            },
        ]
    }

    # Save the test to a JSON file
    test_file = "example_test.json"
    with open(test_file, "w") as f:
        json.dump(test_template, f, indent=2)
    print(f"Test template saved to: {test_file}")

    with Browser(verbose=False) as browser:
        page = browser.new_page()

        # Run the test from dict
        print("\n1. Running test from dict...")
        result = page.run_test(
            test_template,
            continue_on_error=True,
            screenshot_on_error=True,
            verbose=True
        )

        print(f"\n=== Test Results ===")
        print(f"Test Name: {result.test_name}")
        print(f"Total Steps: {result.total_steps}")
        print(f"Executed: {result.executed_steps}")
        print(f"Successful: {result.successful_steps}")
        print(f"Failed: {result.failed_steps}")
        print(f"Execution Time: {result.execution_time:.0f}ms")
        print(f"Success: {result.success}")

        if result.errors:
            print("\nErrors:")
            for error in result.errors:
                print(f"  Step {error.step} ({error.type}): {error.message}")

        # Run the test from file
        print("\n\n2. Running test from JSON file...")
        result2 = page.run_test(test_file, verbose=True)
        print(f"\nFile test result: {result2.successful_steps}/{result2.total_steps} steps passed")

        page.close()

    # Cleanup test file
    os.remove(test_file)

    print("\n=== Example Complete ===")


if __name__ == "__main__":
    main()
