#!/usr/bin/env python3
"""
Fix VM profiles database - Fix device_memory values.
Chrome's navigator.deviceMemory caps at 8.
Valid values: 0.25, 0.5, 1, 2, 4, 8
"""

import re

def main():
    input_file = "/Users/ahstanin/GitHub/Olib-AI/olib-browser/data/profiles/profiles.sql"

    with open(input_file, 'r') as f:
        content = f.read()

    # Pattern: number, device_memory, 'x86_64'
    # Format in file: "    8, 16, 'x86_64'," where 8 is cores, 16 is device_memory
    # We need to cap device_memory at 8

    def fix_device_memory(match):
        cores = match.group(1)
        device_memory = int(match.group(2))
        arch = match.group(3)

        # Cap at 8 (Chrome's maximum)
        if device_memory > 8:
            device_memory = 8

        return f"{cores}, {device_memory}, '{arch}'"

    # Match pattern: number, number, 'x86_64' or 'arm64'
    pattern = r"(\d+),\s*(\d+),\s*'(x86_64|arm64)'"

    new_content, count = re.subn(pattern, fix_device_memory, content)

    # Count how many were actually changed (had values > 8)
    changes = 0
    for match in re.finditer(r"(\d+),\s*(\d+),\s*'(x86_64|arm64)'", content):
        if int(match.group(2)) > 8:
            changes += 1

    with open(input_file, 'w') as f:
        f.write(new_content)

    print(f"✅ Fixed {changes} device_memory values (capped at 8)")

if __name__ == "__main__":
    main()
