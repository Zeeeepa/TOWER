#!/bin/bash
# License Key Management Script
# This script helps manage the RSA key pair used for license signing

set -e

KEY_DIR="$HOME/.owl_license"
PRIVATE_KEY="$KEY_DIR/owl_license.key"
PUBLIC_KEY="$KEY_DIR/owl_license.pub"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "License Key Management for Owl Browser"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  status    - Check current key status"
    echo "  backup    - Backup keys to a secure location"
    echo "  restore   - Restore keys from backup"
    echo "  rotate    - Generate new key pair (WARNING: invalidates all existing licenses!)"
    echo "  update    - Update the embedded key in source code"
    echo ""
}

check_status() {
    echo "=== License Key Status ==="
    echo ""

    if [ -f "$PRIVATE_KEY" ]; then
        echo -e "${GREEN}[OK]${NC} Private key exists: $PRIVATE_KEY"
        echo "    Fingerprint: $(openssl rsa -in "$PRIVATE_KEY" -pubout -outform DER 2>/dev/null | openssl dgst -sha256 | cut -d' ' -f2)"
    else
        echo -e "${RED}[MISSING]${NC} Private key NOT found: $PRIVATE_KEY"
        echo -e "${YELLOW}WARNING: You cannot generate new licenses without the private key!${NC}"
    fi

    echo ""

    if [ -f "$PUBLIC_KEY" ]; then
        echo -e "${GREEN}[OK]${NC} Public key exists: $PUBLIC_KEY"
    else
        echo -e "${RED}[MISSING]${NC} Public key NOT found: $PUBLIC_KEY"
    fi

    echo ""
    echo "=== Embedded Key Check ==="

    # Check if the embedded key matches
    EMBEDDED_CHECK=$(python3 "$SCRIPT_DIR/obfuscate_key.py" 2>/dev/null | head -1)
    if [ -n "$EMBEDDED_CHECK" ]; then
        echo -e "${GREEN}[OK]${NC} Obfuscation script works"
    else
        echo -e "${RED}[ERROR]${NC} Obfuscation script failed"
    fi
}

backup_keys() {
    if [ -z "$1" ]; then
        BACKUP_FILE="owl_license_backup_$(date +%Y%m%d_%H%M%S).tar.gz.enc"
    else
        BACKUP_FILE="$1"
    fi

    echo "=== Backing Up License Keys ==="
    echo ""

    if [ ! -f "$PRIVATE_KEY" ] || [ ! -f "$PUBLIC_KEY" ]; then
        echo -e "${RED}ERROR: Keys not found. Nothing to backup.${NC}"
        exit 1
    fi

    echo "This will create an encrypted backup of your license keys."
    echo "You will be prompted for a password to encrypt the backup."
    echo ""
    echo -e "${YELLOW}IMPORTANT: Remember this password! Without it, the backup is useless.${NC}"
    echo ""

    # Create encrypted tarball
    tar -czf - -C "$KEY_DIR" owl_license.key owl_license.pub | \
        openssl enc -aes-256-cbc -salt -pbkdf2 -out "$BACKUP_FILE"

    echo ""
    echo -e "${GREEN}Backup created: $BACKUP_FILE${NC}"
    echo ""
    echo "Store this file in a SECURE location:"
    echo "  - Password manager"
    echo "  - Encrypted cloud storage"
    echo "  - Physical safe"
    echo ""
    echo -e "${RED}DO NOT commit this file to git!${NC}"
}

restore_keys() {
    if [ -z "$1" ]; then
        echo "Usage: $0 restore <backup_file>"
        exit 1
    fi

    BACKUP_FILE="$1"

    if [ ! -f "$BACKUP_FILE" ]; then
        echo -e "${RED}ERROR: Backup file not found: $BACKUP_FILE${NC}"
        exit 1
    fi

    echo "=== Restoring License Keys ==="
    echo ""

    if [ -f "$PRIVATE_KEY" ]; then
        echo -e "${YELLOW}WARNING: Existing keys will be overwritten!${NC}"
        read -p "Continue? (y/N) " confirm
        if [ "$confirm" != "y" ] && [ "$confirm" != "Y" ]; then
            echo "Aborted."
            exit 0
        fi
    fi

    mkdir -p "$KEY_DIR"

    # Decrypt and extract
    openssl enc -aes-256-cbc -d -salt -pbkdf2 -in "$BACKUP_FILE" | \
        tar -xzf - -C "$KEY_DIR"

    echo ""
    echo -e "${GREEN}Keys restored successfully!${NC}"
    check_status
}

rotate_keys() {
    echo "=== Rotate License Keys ==="
    echo ""
    echo -e "${RED}WARNING: This will generate a NEW key pair!${NC}"
    echo ""
    echo "Consequences:"
    echo "  - ALL existing licenses will become INVALID"
    echo "  - You must rebuild the browser with the new public key"
    echo "  - You must redistribute the browser to all users"
    echo "  - You must issue new licenses to all customers"
    echo ""
    read -p "Are you SURE you want to continue? (type 'ROTATE' to confirm) " confirm

    if [ "$confirm" != "ROTATE" ]; then
        echo "Aborted."
        exit 0
    fi

    # Backup existing keys first
    if [ -f "$PRIVATE_KEY" ]; then
        echo ""
        echo "Backing up existing keys first..."
        backup_keys "owl_license_backup_before_rotate_$(date +%Y%m%d_%H%M%S).tar.gz.enc"
    fi

    echo ""
    echo "Generating new RSA-2048 key pair..."

    mkdir -p "$KEY_DIR"

    # Generate new key pair
    openssl genpkey -algorithm RSA -out "$PRIVATE_KEY" -pkeyopt rsa_keygen_bits:2048
    openssl rsa -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY"

    chmod 600 "$PRIVATE_KEY"
    chmod 644 "$PUBLIC_KEY"

    echo ""
    echo -e "${GREEN}New key pair generated!${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Run: $0 update"
    echo "  2. Rebuild the browser: npm run build:cef"
    echo "  3. Distribute new browser to users"
    echo "  4. Issue new licenses to customers"
}

update_embedded() {
    echo "=== Update Embedded Public Key ==="
    echo ""

    if [ ! -f "$PUBLIC_KEY" ]; then
        echo -e "${RED}ERROR: Public key not found: $PUBLIC_KEY${NC}"
        exit 1
    fi

    # Read the public key
    PUB_KEY_CONTENT=$(cat "$PUBLIC_KEY")

    # Update the obfuscation script
    echo "Updating obfuscate_key.py with current public key..."

    # Create a Python script to update the file
    python3 << EOF
import re

pub_key = '''$PUB_KEY_CONTENT'''

# Read the obfuscation script
with open('$SCRIPT_DIR/obfuscate_key.py', 'r') as f:
    content = f.read()

# Replace the PUBLIC_KEY variable
pattern = r'PUBLIC_KEY = """.*?"""'
replacement = 'PUBLIC_KEY = """' + pub_key + '"""'
new_content = re.sub(pattern, replacement, content, flags=re.DOTALL)

# Write back
with open('$SCRIPT_DIR/obfuscate_key.py', 'w') as f:
    f.write(new_content)

print("Updated obfuscate_key.py")
EOF

    # Generate new obfuscated bytes
    echo ""
    echo "Generating obfuscated key bytes..."
    OBFUSCATED=$(python3 "$SCRIPT_DIR/obfuscate_key.py")

    echo ""
    echo "New obfuscated key bytes generated."
    echo ""
    echo -e "${YELLOW}You need to manually update src/owl_license.cc with these bytes:${NC}"
    echo ""
    echo "$OBFUSCATED"
    echo ""
    echo "After updating, rebuild with: npm run build:cef"
}

# Main
case "$1" in
    status)
        check_status
        ;;
    backup)
        backup_keys "$2"
        ;;
    restore)
        restore_keys "$2"
        ;;
    rotate)
        rotate_keys
        ;;
    update)
        update_embedded
        ;;
    *)
        usage
        ;;
esac
