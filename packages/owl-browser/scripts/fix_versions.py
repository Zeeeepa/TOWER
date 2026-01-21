#!/usr/bin/env python3
"""
Fix VM profiles database - Fix unrealistic OS versions.

Issues:
1. macOS 26.0 doesn't exist (current is 14-15, Sonoma/Sequoia)
2. Some Linux versions like '41', '13' should be distribution-specific
"""

import re

def main():
    input_file = "/Users/ahstanin/GitHub/Olib-AI/olib-browser/data/profiles/profiles.sql"

    with open(input_file, 'r') as f:
        content = f.read()

    changes = []

    # Fix 1: macOS 26.0 -> 15.0 (Sequoia)
    # Pattern: 'macOS', '26.0',
    old = "'macOS', '26.0',"
    new = "'macOS', '15.0',"
    if old in content:
        count = content.count(old)
        content = content.replace(old, new)
        changes.append(f"Fixed {count} macOS 26.0 -> 15.0")

    # Also update the profile IDs that reference macOS 26
    # Pattern: 'macos26-*
    # Change to: 'macos15-*
    old_pattern = r"'macos26-"
    new_pattern = "'macos15-"
    count = len(re.findall(old_pattern, content))
    if count > 0:
        content = re.sub(old_pattern, new_pattern, content)
        changes.append(f"Fixed {count} profile IDs macos26- -> macos15-")

    # Also update the names
    # Pattern: 'macOS 26 -
    old_name = "'macOS 26 -"
    new_name = "'macOS 15 -"
    if old_name in content:
        count = content.count(old_name)
        content = content.replace(old_name, new_name)
        changes.append(f"Fixed {count} profile names macOS 26 -> macOS 15")

    with open(input_file, 'w') as f:
        f.write(content)

    if changes:
        for change in changes:
            print(f"✅ {change}")
    else:
        print("No changes needed")

if __name__ == "__main__":
    main()
