#!/usr/bin/env python3
"""
Validate browser profiles for duplicate fingerprint hashes.

This script parses profiles.sql and checks for duplicate hash values that would
cause fingerprint collisions between different browser profiles.

Usage:
    python3 validate_profiles.py [--verbose]
"""

import re
import sys
from collections import defaultdict
from pathlib import Path


def extract_profiles(sql_content: str) -> list[dict]:
    """Extract profile data from SQL INSERT statements."""
    profiles = []

    # Split by INSERT statements
    # Each profile starts with "INSERT INTO vm_profiles"
    blocks = re.split(r'(?=INSERT INTO vm_profiles)', sql_content, flags=re.IGNORECASE)

    for block in blocks:
        if not block.strip() or 'INSERT INTO vm_profiles' not in block:
            continue

        # Extract profile ID - first quoted string after VALUES (
        id_match = re.search(r"VALUES\s*\(\s*'([^']+)'", block, re.DOTALL | re.IGNORECASE)
        if not id_match:
            continue

        profile_id = id_match.group(1)

        profile = {
            'id': profile_id,
            'audio_hash_seed': None,
            'canvas_hash_seed': None,
            'gpu_renderer_hash_seed': None,
        }

        # Find all 16-character hex strings (hash seeds)
        hex_pattern = re.compile(r"'([A-F0-9]{16})'")
        hex_matches = hex_pattern.findall(block)

        # Hash seeds typically appear in this order in the SQL:
        # 1. gpu_renderer_hash_seed (after gpu params, before webgl_extensions)
        # 2. audio_hash_seed (after screen params)
        # 3. canvas_hash_seed (right after audio_hash_seed)

        if len(hex_matches) >= 3:
            # First hex is gpu_renderer_hash_seed
            profile['gpu_renderer_hash_seed'] = hex_matches[0]
            # Second and third are audio and canvas
            profile['audio_hash_seed'] = hex_matches[1]
            profile['canvas_hash_seed'] = hex_matches[2]
        elif len(hex_matches) == 2:
            # Only audio and canvas (older format without gpu hash)
            profile['audio_hash_seed'] = hex_matches[0]
            profile['canvas_hash_seed'] = hex_matches[1]

        profiles.append(profile)

    return profiles


def find_duplicates(profiles: list[dict], field: str) -> dict[str, list[str]]:
    """Find profiles with duplicate values for a given field."""
    value_to_profiles = defaultdict(list)

    for profile in profiles:
        value = profile.get(field)
        if value:
            value_to_profiles[value].append(profile['id'])

    # Return only values that appear more than once
    return {v: ids for v, ids in value_to_profiles.items() if len(ids) > 1}


def main():
    verbose = '--verbose' in sys.argv or '-v' in sys.argv

    # Find profiles.sql
    script_dir = Path(__file__).parent
    sql_file = script_dir / 'profiles.sql'

    if not sql_file.exists():
        print(f"Error: {sql_file} not found")
        sys.exit(1)

    print(f"Validating profiles in: {sql_file}")
    print()

    # Read and parse SQL
    sql_content = sql_file.read_text()
    profiles = extract_profiles(sql_content)

    print(f"Found {len(profiles)} profiles")
    print()

    if verbose:
        print("Profiles found:")
        for p in profiles:
            print(f"  - {p['id']}")
            print(f"      GPU Hash:    {p['gpu_renderer_hash_seed']}")
            print(f"      Audio Hash:  {p['audio_hash_seed']}")
            print(f"      Canvas Hash: {p['canvas_hash_seed']}")
        print()

    # Check for duplicates
    hash_fields = [
        ('audio_hash_seed', 'Audio Hash'),
        ('canvas_hash_seed', 'Canvas Hash'),
        ('gpu_renderer_hash_seed', 'GPU Renderer Hash'),
    ]

    errors_found = False

    for field, display_name in hash_fields:
        duplicates = find_duplicates(profiles, field)

        if duplicates:
            errors_found = True
            print(f"DUPLICATE {display_name.upper()} VALUES FOUND:")
            for value, profile_ids in duplicates.items():
                print(f"  Hash: {value}")
                for pid in profile_ids:
                    print(f"    - {pid}")
            print()
        elif verbose:
            print(f"  {display_name}: No duplicates")

    # Check for missing hashes
    missing_hashes = []
    for profile in profiles:
        missing = []
        for field, display_name in hash_fields:
            if not profile.get(field):
                missing.append(display_name)
        if missing:
            missing_hashes.append((profile['id'], missing))

    if missing_hashes:
        print("WARNING: Profiles with missing hash seeds:")
        for pid, missing in missing_hashes:
            print(f"  {pid}: missing {', '.join(missing)}")
        print()

    # Summary
    print("-" * 60)
    if errors_found:
        print("VALIDATION FAILED: Duplicate hash seeds detected!")
        print("Each profile must have unique hash seeds to avoid fingerprint collisions.")
        print()
        print("Generate new unique seeds with:")
        print("  python3 -c \"import secrets; print(secrets.token_hex(8).upper())\"")
        sys.exit(1)
    else:
        print(f"VALIDATION PASSED: All {len(profiles)} profiles have unique hash seeds")
        sys.exit(0)


if __name__ == '__main__':
    main()
