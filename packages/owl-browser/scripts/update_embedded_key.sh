#!/bin/bash
#
# Update Embedded Public Key Script
# ==================================
# This script reads the public key from ~/.owl_license/owl_license.pub,
# obfuscates it, and updates src/owl_license.cc automatically.
#
# Usage:
#   ./scripts/update_embedded_key.sh              # Use default key path
#   ./scripts/update_embedded_key.sh /path/to/key.pub  # Use custom key path
#   ./scripts/update_embedded_key.sh --check      # Verify keys match without updating
#
# This is typically run after:
#   1. Setting up on a new machine with existing keys
#   2. Rotating keys with manage_license_keys.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LICENSE_CC="$PROJECT_ROOT/src/owl_license.cc"
OBFUSCATE_SCRIPT="$SCRIPT_DIR/obfuscate_key.py"

DEFAULT_KEY_PATH="$HOME/.owl_license/owl_license.pub"
PRIVATE_KEY_PATH="$HOME/.owl_license/owl_license.key"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
KEY_PATH="$DEFAULT_KEY_PATH"
CHECK_ONLY=false

for arg in "$@"; do
    case $arg in
        --check)
            CHECK_ONLY=true
            ;;
        --help|-h)
            echo "Update Embedded Public Key"
            echo ""
            echo "Usage:"
            echo "  $0                        Update embedded key from ~/.owl_license/owl_license.pub"
            echo "  $0 /path/to/key.pub       Update embedded key from specified path"
            echo "  $0 --check                Verify current embedded key matches key file"
            echo ""
            echo "This script:"
            echo "  1. Validates the public key exists and is valid"
            echo "  2. Generates XOR-obfuscated bytes using obfuscate_key.py"
            echo "  3. Updates src/owl_license.cc with the new obfuscated key"
            echo ""
            echo "After running, rebuild with: npm run build"
            exit 0
            ;;
        -*)
            echo -e "${RED}Unknown option: $arg${NC}"
            exit 1
            ;;
        *)
            KEY_PATH="$arg"
            ;;
    esac
done

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}   Update Embedded Public Key${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Step 1: Validate prerequisites
echo -e "${YELLOW}Step 1: Validating prerequisites...${NC}"

if [ ! -f "$OBFUSCATE_SCRIPT" ]; then
    echo -e "${RED}ERROR: Obfuscation script not found: $OBFUSCATE_SCRIPT${NC}"
    exit 1
fi

if [ ! -f "$LICENSE_CC" ]; then
    echo -e "${RED}ERROR: Source file not found: $LICENSE_CC${NC}"
    exit 1
fi

if [ ! -f "$KEY_PATH" ]; then
    echo -e "${RED}ERROR: Public key not found: $KEY_PATH${NC}"
    echo ""
    echo "To generate a key pair, run:"
    echo "  ./build/license/license_generator keygen"
    echo ""
    echo "Or restore from backup:"
    echo "  ./scripts/manage_license_keys.sh restore /path/to/backup.tar.gz.enc"
    exit 1
fi

echo -e "  ${GREEN}✓${NC} Obfuscation script found"
echo -e "  ${GREEN}✓${NC} Source file found"
echo -e "  ${GREEN}✓${NC} Public key found: $KEY_PATH"

# Validate key format
if ! head -1 "$KEY_PATH" | grep -q "BEGIN PUBLIC KEY"; then
    echo -e "${RED}ERROR: File does not appear to be a valid PEM public key${NC}"
    exit 1
fi
echo -e "  ${GREEN}✓${NC} Public key format valid"

# Check if private key also exists (warning if not)
if [ ! -f "$PRIVATE_KEY_PATH" ]; then
    echo -e "  ${YELLOW}⚠${NC}  Private key not found (you won't be able to generate licenses)"
fi

echo ""

# Step 2: Generate obfuscated key
echo -e "${YELLOW}Step 2: Generating obfuscated key...${NC}"

OBFUSCATED_OUTPUT=$(python3 "$OBFUSCATE_SCRIPT" "$KEY_PATH" 2>&1)
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: Failed to generate obfuscated key${NC}"
    echo "$OBFUSCATED_OUTPUT"
    exit 1
fi

# Extract just the C array part (between the braces)
ARRAY_START=$(echo "$OBFUSCATED_OUTPUT" | grep -n "static const uint8_t kObfuscatedPublicKey" | cut -d: -f1)
ARRAY_END=$(echo "$OBFUSCATED_OUTPUT" | grep -n "static const size_t kObfuscatedPublicKeySize" | cut -d: -f1)

if [ -z "$ARRAY_START" ] || [ -z "$ARRAY_END" ]; then
    echo -e "${RED}ERROR: Could not parse obfuscated output${NC}"
    exit 1
fi

# Get key size from output
KEY_SIZE=$(echo "$OBFUSCATED_OUTPUT" | grep "kObfuscatedPublicKeySize" | grep -o '[0-9]*')
echo -e "  ${GREEN}✓${NC} Generated obfuscated key ($KEY_SIZE bytes)"

if $CHECK_ONLY; then
    echo ""
    echo -e "${YELLOW}Step 3: Checking if embedded key matches...${NC}"

    # Extract current embedded key size
    CURRENT_SIZE=$(grep "static const size_t kObfuscatedPublicKeySize" "$LICENSE_CC" | grep -o '[0-9]*')

    if [ "$CURRENT_SIZE" = "$KEY_SIZE" ]; then
        echo -e "  ${GREEN}✓${NC} Key sizes match ($KEY_SIZE bytes)"

        # More thorough check - compare first few bytes
        NEW_FIRST_LINE=$(echo "$OBFUSCATED_OUTPUT" | grep -A1 "kObfuscatedPublicKey\[\]" | tail -1 | tr -d ' ')
        CURRENT_FIRST_LINE=$(grep -A1 "kObfuscatedPublicKey\[\]" "$LICENSE_CC" | tail -1 | tr -d ' ')

        if [ "$NEW_FIRST_LINE" = "$CURRENT_FIRST_LINE" ]; then
            echo -e "  ${GREEN}✓${NC} Embedded key matches current public key"
            echo ""
            echo -e "${GREEN}No update needed - keys are in sync.${NC}"
        else
            echo -e "  ${YELLOW}⚠${NC}  Embedded key differs from current public key"
            echo ""
            echo -e "${YELLOW}Run without --check to update the embedded key.${NC}"
        fi
    else
        echo -e "  ${YELLOW}⚠${NC}  Key sizes differ (embedded: $CURRENT_SIZE, current: $KEY_SIZE)"
        echo ""
        echo -e "${YELLOW}Run without --check to update the embedded key.${NC}"
    fi
    exit 0
fi

# Step 3: Update owl_license.cc
echo ""
echo -e "${YELLOW}Step 3: Updating src/owl_license.cc...${NC}"

# Create backup
BACKUP_FILE="${LICENSE_CC}.backup.$(date +%Y%m%d_%H%M%S)"
cp "$LICENSE_CC" "$BACKUP_FILE"
echo -e "  ${GREEN}✓${NC} Created backup: $(basename $BACKUP_FILE)"

# Create a temp file with the new content
TEMP_FILE=$(mktemp)

# Use Python to do the replacement (more reliable for multi-line)
python3 << EOF
import re
import sys

# Read the source file
with open('$LICENSE_CC', 'r') as f:
    content = f.read()

# Read the new obfuscated output
new_array = '''$OBFUSCATED_OUTPUT'''

# Extract just the array definition and size
array_match = re.search(r'(static const uint8_t kObfuscatedPublicKey\[\] = \{[\s\S]*?\};)\s*(static const size_t kObfuscatedPublicKeySize = \d+;)', new_array)
if not array_match:
    print("ERROR: Could not parse new obfuscated array", file=sys.stderr)
    sys.exit(1)

new_array_def = array_match.group(1)
new_size_def = array_match.group(2)

# Pattern to match the existing array and size in the source
pattern = r'static const uint8_t kObfuscatedPublicKey\[\] = \{[\s\S]*?\};\s*static const size_t kObfuscatedPublicKeySize = \d+;'

# Check if pattern exists
if not re.search(pattern, content):
    print("ERROR: Could not find existing kObfuscatedPublicKey in source file", file=sys.stderr)
    sys.exit(1)

# Replace
new_content = re.sub(pattern, new_array_def + '\n' + new_size_def, content)

# Write to temp file
with open('$TEMP_FILE', 'w') as f:
    f.write(new_content)

print("OK")
EOF

RESULT=$?
if [ $RESULT -ne 0 ]; then
    echo -e "${RED}ERROR: Failed to update source file${NC}"
    rm -f "$TEMP_FILE"
    exit 1
fi

# Move temp file to target
mv "$TEMP_FILE" "$LICENSE_CC"
echo -e "  ${GREEN}✓${NC} Updated kObfuscatedPublicKey array"
echo -e "  ${GREEN}✓${NC} Updated kObfuscatedPublicKeySize to $KEY_SIZE"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}   Public key updated successfully!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Next steps:"
echo "  1. Rebuild the browser: npm run build"
echo "  2. Test license validation: ./build/Release/owl_browser.app/Contents/MacOS/owl_browser --license status"
echo ""
echo -e "${YELLOW}Note: Previously issued licenses will continue to work${NC}"
echo -e "${YELLOW}      as long as you use the same private key to sign them.${NC}"
echo ""
