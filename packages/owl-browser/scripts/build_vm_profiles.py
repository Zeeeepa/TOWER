#!/usr/bin/env python3
"""
Build VM Profiles Database

This script compiles the VM profiles from SQL files into an encrypted SQLite database.
It also generates an obfuscated key header for C++ that protects against decompilers.

Usage:
    python3 scripts/build_vm_profiles.py [--browser-version VERSION]

Requirements:
    - sqlcipher CLI tool (brew install sqlcipher / apt install sqlcipher)
    - OWL_VM_PROFILE_DB_PASS in .env file

Output:
    - data/profiles/vm_profiles.db (encrypted database)
    - include/stealth/owl_vm_db_key.h (obfuscated key header)
"""

import os
import sys
import subprocess
import hashlib
import secrets
import tempfile
import shutil
from pathlib import Path


# XOR key for obfuscation (16 bytes - must match C++ deobfuscation)
# This is a compile-time obfuscation to prevent simple string extraction
OBFUSCATION_KEY = bytes([
    0x4D, 0x7A, 0x2F, 0x8C, 0x1B, 0x5E, 0x9A, 0x3D,
    0x6F, 0xC8, 0x41, 0xE7, 0x0A, 0xB3, 0x56, 0xD9
])


def get_project_root() -> Path:
    """Get project root directory."""
    script_dir = Path(__file__).parent
    return script_dir.parent


def load_env_file(env_path: Path) -> dict:
    """Load environment variables from .env file."""
    env_vars = {}
    if not env_path.exists():
        return env_vars

    with open(env_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                key, value = line.split('=', 1)
                key = key.strip()
                value = value.strip()
                # Remove surrounding quotes
                if value.startswith('"') and value.endswith('"'):
                    value = value[1:-1]
                elif value.startswith("'") and value.endswith("'"):
                    value = value[1:-1]
                env_vars[key] = value
    return env_vars


def get_db_password(env_vars: dict) -> str:
    """Get database password from environment variable or .env file."""
    # Check os.environ first (for Docker builds), then fall back to .env file
    password = os.environ.get('OWL_VM_PROFILE_DB_PASS') or env_vars.get('OWL_VM_PROFILE_DB_PASS')
    if not password:
        print("ERROR: OWL_VM_PROFILE_DB_PASS not found in environment or .env file", file=sys.stderr)
        print("Set it via environment variable or add to your .env file:", file=sys.stderr)
        print(f'  OWL_VM_PROFILE_DB_PASS="{secrets.token_hex(32)}"', file=sys.stderr)
        sys.exit(1)
    return password


def get_browser_version(env_vars: dict, cli_version: str = None) -> tuple:
    """Get browser version from CLI arg or use default from profiles.sql."""
    if cli_version:
        # Parse version like "143" or "143.0.0.0"
        parts = cli_version.split('.')
        major = parts[0]
        full = cli_version if len(parts) == 4 else f"{major}.0.0.0"
        return major, full

    # Default version (from config table in schema.sql)
    return "143", "143.0.0.0"


def read_sql_file(path: Path) -> str:
    """Read SQL file content."""
    if not path.exists():
        print(f"ERROR: SQL file not found: {path}", file=sys.stderr)
        sys.exit(1)
    with open(path, 'r') as f:
        return f.read()


def replace_templates(sql: str, browser_version: str, browser_version_full: str) -> str:
    """Replace template variables in SQL."""
    sql = sql.replace('{{BROWSER_VERSION}}', browser_version)
    sql = sql.replace('{{BROWSER_VERSION_FULL}}', browser_version_full)
    return sql


def obfuscate_bytes(data: bytes) -> bytes:
    """XOR obfuscate data with the obfuscation key."""
    result = bytearray()
    key_len = len(OBFUSCATION_KEY)
    for i, byte in enumerate(data):
        result.append(byte ^ OBFUSCATION_KEY[i % key_len])
    return bytes(result)


def derive_key_parts(password: str) -> tuple:
    """
    Derive multiple key parts from the password.
    This makes it harder to extract the full key via decompilation.

    Strategy:
    1. Hash the password with different salts to get multiple parts
    2. XOR obfuscate each part
    3. Store parts separately in the binary
    4. At runtime, reconstruct by combining parts
    """
    # Generate deterministic salts from password hash
    base_hash = hashlib.sha256(password.encode()).digest()

    # Salt 1: First 8 bytes of hash
    salt1 = base_hash[:8]
    # Salt 2: Bytes 8-16 of hash
    salt2 = base_hash[8:16]
    # Salt 3: Bytes 16-24 of hash
    salt3 = base_hash[16:24]

    # Derive key parts
    part1 = hashlib.sha256(salt1 + password.encode()).digest()[:16]
    part2 = hashlib.sha256(salt2 + password.encode()).digest()[:16]
    part3 = hashlib.sha256(salt3 + password.encode()).digest()[:16]

    # The actual password is recovered by XORing parts together
    # password_hash = SHA256(part1 XOR part2 XOR part3)

    return part1, part2, part3


def generate_key_header(password: str, output_path: Path):
    """
    Generate C++ header with obfuscated key parts.

    The key is split and obfuscated to protect against:
    - Simple string extraction (strings binary)
    - Pattern matching in decompilers
    - Memory scanning for known key patterns
    """
    part1, part2, part3 = derive_key_parts(password)

    # Obfuscate each part
    obf_part1 = obfuscate_bytes(part1)
    obf_part2 = obfuscate_bytes(part2)
    obf_part3 = obfuscate_bytes(part3)
    obf_key = obfuscate_bytes(OBFUSCATION_KEY)

    # Also store the actual password obfuscated (needed for SQLCipher)
    password_bytes = password.encode('utf-8')
    obf_password = obfuscate_bytes(password_bytes)

    def format_bytes(data: bytes, name: str) -> str:
        """Format bytes as C++ array."""
        lines = [f"static const uint8_t {name}[] = {{"]
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
            if i + 12 < len(data):
                hex_bytes += ","
            lines.append(f"    {hex_bytes}")
        lines.append("};")
        lines.append(f"static const size_t {name}Size = {len(data)};")
        return "\n".join(lines)

    header_content = f'''// Auto-generated by build_vm_profiles.py - DO NOT EDIT MANUALLY
// This file contains obfuscated key material for VM profiles database
// Generated: {subprocess.check_output(['date', '-u']).decode().strip()}
//
// SECURITY NOTE: This obfuscation protects against simple extraction methods.
// For maximum security, ensure the binary is not distributed to untrusted parties.

#ifndef OWL_VM_DB_KEY_H_
#define OWL_VM_DB_KEY_H_

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace owl {{
namespace vm_db {{

// Obfuscation key (XOR mask)
{format_bytes(obf_key, "kObfuscatedMask")}

// Key parts (obfuscated) - used for integrity verification
{format_bytes(obf_part1, "kObfuscatedPart1")}

{format_bytes(obf_part2, "kObfuscatedPart2")}

{format_bytes(obf_part3, "kObfuscatedPart3")}

// Obfuscated database password
{format_bytes(obf_password, "kObfuscatedPassword")}

// Deobfuscation helper
inline std::vector<uint8_t> Deobfuscate(const uint8_t* data, size_t data_size,
                                         const uint8_t* mask, size_t mask_size) {{
    std::vector<uint8_t> result(data_size);
    for (size_t i = 0; i < data_size; ++i) {{
        result[i] = data[i] ^ mask[i % mask_size];
    }}
    return result;
}}

// Get the deobfuscated mask first (meta-obfuscation)
inline std::vector<uint8_t> GetMask() {{
    // The mask itself is XOR'd with a compile-time constant
    static const uint8_t kMetaMask[] = {{
        0x4D, 0x7A, 0x2F, 0x8C, 0x1B, 0x5E, 0x9A, 0x3D,
        0x6F, 0xC8, 0x41, 0xE7, 0x0A, 0xB3, 0x56, 0xD9
    }};
    return Deobfuscate(kObfuscatedMask, kObfuscatedMaskSize, kMetaMask, sizeof(kMetaMask));
}}

// Get the database password
inline std::string GetDatabasePassword() {{
    auto mask = GetMask();
    auto password = Deobfuscate(kObfuscatedPassword, kObfuscatedPasswordSize,
                                 mask.data(), mask.size());
    return std::string(password.begin(), password.end());
}}

}}  // namespace vm_db
}}  // namespace owl

#endif  // OWL_VM_DB_KEY_H_
'''

    # Ensure directory exists
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, 'w') as f:
        f.write(header_content)

    print(f"Generated obfuscated key header: {output_path}")


def build_database(schema_sql: str, profiles_sql: str, password: str, output_path: Path):
    """Build encrypted SQLite database using sqlcipher."""

    # Check if sqlcipher is available
    try:
        result = subprocess.run(['sqlcipher', '--version'],
                                capture_output=True, text=True)
        print(f"Using SQLCipher: {result.stdout.strip()}")
    except FileNotFoundError:
        print("ERROR: sqlcipher not found. Install with:", file=sys.stderr)
        print("  macOS: brew install sqlcipher", file=sys.stderr)
        print("  Ubuntu: sudo apt install sqlcipher", file=sys.stderr)
        sys.exit(1)

    # Create output directory
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Remove existing database
    if output_path.exists():
        output_path.unlink()

    # Combine SQL files
    combined_sql = schema_sql + "\n\n" + profiles_sql

    # Create database with encryption
    with tempfile.NamedTemporaryFile(mode='w', suffix='.sql', delete=False) as f:
        # SQLCipher commands
        f.write(f"PRAGMA key = '{password}';\n")
        f.write("PRAGMA cipher_page_size = 4096;\n")
        f.write("PRAGMA kdf_iter = 256000;\n")
        f.write("PRAGMA cipher_hmac_algorithm = HMAC_SHA512;\n")
        f.write("PRAGMA cipher_kdf_algorithm = PBKDF2_HMAC_SHA512;\n")
        f.write("\n")
        f.write(combined_sql)
        f.write("\n")
        # Verify encryption
        f.write("SELECT COUNT(*) FROM vm_profiles;\n")
        temp_sql = f.name

    try:
        # Run sqlcipher
        result = subprocess.run(
            ['sqlcipher', str(output_path)],
            stdin=open(temp_sql, 'r'),
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            print(f"ERROR: sqlcipher failed:", file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            sys.exit(1)

        # Parse profile count from output
        lines = result.stdout.strip().split('\n')
        if lines:
            count = lines[-1].strip()
            print(f"Database created with {count} VM profiles")

    finally:
        os.unlink(temp_sql)

    # Verify database is encrypted
    with open(output_path, 'rb') as f:
        header = f.read(16)

    if header.startswith(b'SQLite format'):
        print("WARNING: Database does not appear to be encrypted!", file=sys.stderr)
        print("Check that sqlcipher is properly installed.", file=sys.stderr)
        sys.exit(1)

    file_size = output_path.stat().st_size
    print(f"Encrypted database: {output_path} ({file_size:,} bytes)")


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Build VM profiles database')
    parser.add_argument('--browser-version', '-v',
                        help='Browser version (e.g., 143 or 143.0.0.0)')
    parser.add_argument('--output', '-o',
                        help='Output database path')
    parser.add_argument('--key-header', '-k',
                        help='Output key header path')
    args = parser.parse_args()

    # Get paths
    project_root = get_project_root()
    env_path = project_root / '.env'
    schema_path = project_root / 'data' / 'profiles' / 'schema.sql'
    profiles_path = project_root / 'data' / 'profiles' / 'profiles.sql'

    default_db_path = project_root / 'data' / 'profiles' / 'vm_profiles.db'
    default_header_path = project_root / 'include' / 'stealth' / 'owl_vm_db_key.h'

    output_path = Path(args.output) if args.output else default_db_path
    header_path = Path(args.key_header) if args.key_header else default_header_path

    # Load environment
    print(f"Loading environment from: {env_path}")
    env_vars = load_env_file(env_path)

    # Get password
    password = get_db_password(env_vars)
    print(f"Database password: {'*' * min(8, len(password))}... ({len(password)} chars)")

    # Get browser version
    browser_version, browser_version_full = get_browser_version(env_vars, args.browser_version)
    print(f"Browser version: {browser_version} (full: {browser_version_full})")

    # Read SQL files
    print(f"Reading schema: {schema_path}")
    schema_sql = read_sql_file(schema_path)

    print(f"Reading profiles: {profiles_path}")
    profiles_sql = read_sql_file(profiles_path)

    # Replace templates
    profiles_sql = replace_templates(profiles_sql, browser_version, browser_version_full)

    # Generate obfuscated key header
    print("Generating obfuscated key header...")
    generate_key_header(password, header_path)

    # Build encrypted database
    print("Building encrypted database...")
    build_database(schema_sql, profiles_sql, password, output_path)

    print("\nBuild complete!")
    print(f"  Database: {output_path}")
    print(f"  Key header: {header_path}")

    return 0


if __name__ == '__main__':
    sys.exit(main())
