#!/usr/bin/env python3
"""
Fix VM profiles database - Phase 2:
Fix gpu_unmasked_renderer lines that come after fixed vendor lines.
"""

import re

def get_proper_unmasked_renderer(renderer_str):
    """Extract GPU model from ANGLE renderer string."""
    if renderer_str.startswith("ANGLE"):
        # Find content between first comma and "Direct3D" or ", OpenGL"
        match = re.search(r"ANGLE \([^,]+,\s*(.+?)(?:\s+Direct3D|\s*,\s*OpenGL)", renderer_str)
        if match:
            return match.group(1).strip()
        # Fallback: extract second part between commas
        parts = renderer_str.split(",")
        if len(parts) >= 2:
            return parts[1].strip()
    return renderer_str

def main():
    input_file = "/Users/ahstanin/GitHub/Olib-AI/olib-browser/data/profiles/profiles.sql"

    with open(input_file, 'r') as f:
        content = f.read()

    lines = content.split('\n')
    fixed_lines = []
    changes = 0

    # Fixed unmasked vendors to look for
    fixed_vendors = ["'Intel Inc.'", "'NVIDIA Corporation'", "'AMD'", "'Apple Inc.'"]

    i = 0
    while i < len(lines):
        line = lines[i]

        # Check if this is a fixed unmasked vendor line
        is_fixed_vendor = any(v in line for v in fixed_vendors)

        if is_fixed_vendor and line.strip().endswith(","):
            fixed_lines.append(line)
            i += 1

            # Next line should be gpu_unmasked_renderer - check if it needs fixing
            if i < len(lines) and "'ANGLE" in lines[i]:
                renderer_match = re.match(r"(\s+)'(ANGLE .+)'(,\s*)$", lines[i])
                if renderer_match:
                    indent = renderer_match.group(1)
                    old_renderer = renderer_match.group(2)
                    comma = renderer_match.group(3)
                    new_renderer = get_proper_unmasked_renderer(old_renderer)
                    if new_renderer != old_renderer:
                        fixed_lines.append(f"{indent}'{new_renderer}'{comma}")
                        changes += 1
                        i += 1
                        continue
            # No fix needed or couldn't match
            if i < len(lines):
                fixed_lines.append(lines[i])
                i += 1
        else:
            fixed_lines.append(line)
            i += 1

    # Write back
    with open(input_file, 'w') as f:
        f.write('\n'.join(fixed_lines))

    print(f"✅ Fixed {changes} unmasked renderer values in profiles.sql")

if __name__ == "__main__":
    main()
