#!/usr/bin/env python3
"""
Remote Template Test Example

Demonstrates running a JSON test template against a remote Owl Browser HTTP server
with video recording. Similar to the Node.js SDK's remote-template-test.js.

Prerequisites:
    Start the HTTP server first on your remote machine:
    ./owl_browser --http --port 8080 --token "your-secret-token"
"""

import sys
import os
import json
import time

# Add parent directory to path for local development
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from owl_browser import Browser, RemoteConfig


def main():
    # Configuration - override with environment variables
    http_url = os.environ.get("OWL_HTTP_URL", "http://10.0.0.43:8080")
    http_token = os.environ.get("OWL_HTTP_TOKEN", "your-secret-token")
    template_path = os.path.join(os.path.dirname(__file__), "custom-test.json")

    print("=" * 60)
    print("  Owl Browser Python SDK - Remote Template Test with Video")
    print("=" * 60)
    print(f"  Server: {http_url}")
    print(f"  Template: {template_path}")
    print("=" * 60)

    # Load the template
    if not os.path.exists(template_path):
        print(f"Error: Template file not found: {template_path}")
        print("Please copy custom-test.json to the examples directory")
        sys.exit(1)

    with open(template_path, 'r') as f:
        template = json.load(f)

    timestamp = time.strftime("%H:%M:%S")
    print(f"\n[{timestamp}] Loaded template: \"{template.get('name', 'Unnamed')}\"")
    print(f"[{timestamp}] Description: {template.get('description', 'No description')}")

    steps = [s for s in template.get('steps', []) if s.get('selected', True)]
    print(f"[{timestamp}] Steps: {len(steps)}")

    # Configure remote connection
    remote_config = RemoteConfig(
        url=http_url,
        token=http_token,
        timeout=60000,
        verify_ssl=False
    )

    print(f"\n[{time.strftime('%H:%M:%S')}] Connecting to remote browser server...")

    try:
        with Browser(remote=remote_config, verbose=False) as browser:
            print(f"[{time.strftime('%H:%M:%S')}] Connected!")

            # Create a new page
            print(f"[{time.strftime('%H:%M:%S')}] Creating new page...")
            page = browser.new_page()
            print(f"[{time.strftime('%H:%M:%S')}] Page created (context: {page.id})")

            # Start video recording
            print(f"[{time.strftime('%H:%M:%S')}] Starting video recording...")
            page.start_video_recording(fps=30)
            print(f"[{time.strftime('%H:%M:%S')}] Video recording started!")

            # Print test steps preview
            print("\n" + "-" * 60)
            print("  Test Steps:")
            print("-" * 60)
            for i, step in enumerate(steps, 1):
                step_type = step.get('type', 'unknown')
                details = ""
                if step.get('url'):
                    details = step['url']
                elif step.get('selector'):
                    details = f'"{step["selector"]}"'
                    if step.get('text'):
                        details += f' -> "{step["text"]}"'
                    elif step.get('value'):
                        details += f' -> "{step["value"]}"'
                print(f"  {i}. {step_type}: {details}" if details else f"  {i}. {step_type}")
            print("-" * 60)

            # Execute the test
            print(f"\n[{time.strftime('%H:%M:%S')}] Executing test template...")
            result = page.run_test(
                template,
                continue_on_error=True,
                screenshot_on_error=True,
                verbose=True
            )

            # Stop video recording
            print(f"[{time.strftime('%H:%M:%S')}] Stopping video recording...")
            video_path = page.stop_video_recording()
            print(f"[{time.strftime('%H:%M:%S')}] Video saved to: {video_path}")

            # Take final screenshot
            print(f"[{time.strftime('%H:%M:%S')}] Taking final screenshot...")
            output_dir = os.path.join(os.path.dirname(__file__), "..", "output")
            os.makedirs(output_dir, exist_ok=True)
            screenshot_path = os.path.join(output_dir, "template-result.png")
            page.screenshot(screenshot_path)
            print(f"[{time.strftime('%H:%M:%S')}] Final screenshot saved to: {screenshot_path}")

            # Print results
            print("\n" + "=" * 60)
            print("  Test Results")
            print("=" * 60)
            print(f"  Test Name:      {result.test_name}")
            status = "PASSED" if result.success else "FAILED"
            print(f"  Success:        {'PASSED' if result.success else 'FAILED'}")
            print(f"  Total Steps:    {result.total_steps}")
            print(f"  Executed:       {result.executed_steps}")
            print(f"  Successful:     {result.successful_steps}")
            print(f"  Failed:         {result.failed_steps}")
            print(f"  Execution Time: {result.execution_time:.0f}ms ({result.execution_time/1000:.1f}s)")
            print("=" * 60)

            if result.errors:
                print("\n  Errors:")
                print("-" * 60)
                for error in result.errors:
                    print(f"  Step {error.step} ({error.type}): {error.message}")
                print("-" * 60)

            print("\n  Output Files:")
            print("-" * 60)
            print(f"  Video:      {video_path}")
            print(f"  Screenshot: {screenshot_path}")
            print("-" * 60)

            # Cleanup
            print(f"\n[{time.strftime('%H:%M:%S')}] Page closed")
            page.close()

    except Exception as e:
        print(f"\nError: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    print(f"[{time.strftime('%H:%M:%S')}] Disconnected from remote browser")

    # Return exit code based on test result
    sys.exit(0 if result.success else 1)


if __name__ == "__main__":
    main()
