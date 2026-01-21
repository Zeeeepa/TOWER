#!/bin/bash
#
# Build script for Owl Browser License Generator
#
# This script compiles the license_generator.cpp into a standalone binary.
# The binary is used to generate cryptographically signed license files.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/../build/license"

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "Building Olib License Generator..."

# Detect platform
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Platform: macOS"

    # Compile with macOS frameworks
    clang++ -std=c++17 -O2 \
        -framework Security \
        -framework CoreFoundation \
        -framework IOKit \
        -o "$OUTPUT_DIR/license_generator" \
        "$SCRIPT_DIR/license_generator.cpp"

    echo "Build successful: $OUTPUT_DIR/license_generator"

elif [[ "$OSTYPE" == "linux"* ]]; then
    echo "Platform: Linux"

    # Check for OpenSSL
    if ! pkg-config --exists openssl; then
        echo "Error: OpenSSL development libraries not found."
        echo "Install with: sudo apt-get install libssl-dev"
        exit 1
    fi

    # Compile with OpenSSL
    g++ -std=c++17 -O2 \
        $(pkg-config --cflags openssl) \
        -o "$OUTPUT_DIR/license_generator" \
        "$SCRIPT_DIR/license_generator.cpp" \
        $(pkg-config --libs openssl)

    echo "Build successful: $OUTPUT_DIR/license_generator"

else
    echo "Error: Unsupported platform: $OSTYPE"
    exit 1
fi

echo ""
echo "Usage:"
echo "  $OUTPUT_DIR/license_generator keygen"
echo "  $OUTPUT_DIR/license_generator generate --name 'John Doe' --email 'john@example.com' --type professional --output license.olic"
echo "  $OUTPUT_DIR/license_generator verify license.olic"
echo "  $OUTPUT_DIR/license_generator info license.olic"
