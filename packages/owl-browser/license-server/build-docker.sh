#!/bin/bash
# ==============================================================================
# Owl Browser License Server - Docker Build Script
# ==============================================================================
# This script prepares the Docker build context by copying necessary files
# from the parent directory, then builds the Docker image.
#
# Usage:
#   ./build-docker.sh              # Build the image
#   ./build-docker.sh --no-cache   # Build without cache
#   ./build-docker.sh --push       # Build and push to registry
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=============================================="
echo -e "  Owl Browser License Server - Docker Build"
echo -e "==============================================${NC}"
echo ""

# Parse arguments
NO_CACHE=""
PUSH=""
for arg in "$@"; do
    case $arg in
        --no-cache)
            NO_CACHE="--no-cache"
            ;;
        --push)
            PUSH="true"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --no-cache    Build without using cache"
            echo "  --push        Push image to registry after build"
            echo "  --help        Show this help message"
            exit 0
            ;;
    esac
done

# ==============================================================================
# Step 1: Copy license_generator.cpp to docker/ directory
# ==============================================================================
echo -e "${YELLOW}[1/4] Preparing build context...${NC}"

LICENSE_GEN_SRC="$PROJECT_ROOT/scripts/license_generator.cpp"
LICENSE_GEN_DST="$SCRIPT_DIR/docker/license_generator.cpp"

if [ ! -f "$LICENSE_GEN_SRC" ]; then
    echo -e "${RED}ERROR: license_generator.cpp not found at:${NC}"
    echo "  $LICENSE_GEN_SRC"
    exit 1
fi

mkdir -p "$SCRIPT_DIR/docker"
cp "$LICENSE_GEN_SRC" "$LICENSE_GEN_DST"
echo -e "  ${GREEN}✓${NC} Copied license_generator.cpp to docker/"

# ==============================================================================
# Step 2: Create directories for volumes
# ==============================================================================
echo -e "${YELLOW}[2/4] Creating volume directories...${NC}"

mkdir -p "$SCRIPT_DIR/keys"
mkdir -p "$SCRIPT_DIR/data"
mkdir -p "$SCRIPT_DIR/licenses"
mkdir -p "$SCRIPT_DIR/logs"

echo -e "  ${GREEN}✓${NC} Created: keys/, data/, licenses/, logs/"

# ==============================================================================
# Step 3: Check for RSA keys
# ==============================================================================
echo -e "${YELLOW}[3/4] Checking RSA keys...${NC}"

PRIVATE_KEY="$SCRIPT_DIR/keys/private_key.pem"
PUBLIC_KEY="$SCRIPT_DIR/keys/public_key.pem"

# Check standard location first
HOME_KEY_DIR="$HOME/.owl_license"
if [ -f "$HOME_KEY_DIR/owl_license.key" ] && [ -f "$HOME_KEY_DIR/owl_license.pub" ]; then
    if [ ! -f "$PRIVATE_KEY" ] || [ ! -f "$PUBLIC_KEY" ]; then
        echo -e "  ${GREEN}✓${NC} Found keys at ~/.owl_license/"
        echo "  -> Copying to license-server/keys/"
        cp "$HOME_KEY_DIR/owl_license.key" "$PRIVATE_KEY"
        cp "$HOME_KEY_DIR/owl_license.pub" "$PUBLIC_KEY"
        chmod 600 "$PRIVATE_KEY"
        chmod 644 "$PUBLIC_KEY"
    fi
fi

if [ -f "$PRIVATE_KEY" ] && [ -f "$PUBLIC_KEY" ]; then
    echo -e "  ${GREEN}✓${NC} RSA keys found in keys/ directory"
else
    echo -e "  ${YELLOW}!${NC} RSA keys NOT found in keys/ directory"
    echo ""
    echo "  You have two options:"
    echo "  1. Copy your existing keys to license-server/keys/"
    echo "     cp ~/.owl_license/owl_license.key keys/private_key.pem"
    echo "     cp ~/.owl_license/owl_license.pub keys/public_key.pem"
    echo ""
    echo "  2. Set GENERATE_KEYS=true in .env to auto-generate"
    echo "     (Keys will be generated on first container start)"
    echo ""
    echo -e "  ${YELLOW}WARNING: New keys will invalidate existing licenses!${NC}"
    echo ""
fi

# ==============================================================================
# Step 4: Build Docker image
# ==============================================================================
echo -e "${YELLOW}[4/4] Building Docker image...${NC}"
echo ""

cd "$SCRIPT_DIR"

docker build $NO_CACHE -t owl-license-server:latest .

BUILD_STATUS=$?

# Cleanup
rm -f "$LICENSE_GEN_DST"

if [ $BUILD_STATUS -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=============================================="
    echo -e "  Build completed successfully!"
    echo -e "==============================================${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Copy .env.example to .env and configure"
    echo "  2. Ensure RSA keys are in keys/ directory"
    echo "  3. Run: docker-compose up -d"
    echo ""
    echo "Services will be available at:"
    echo "  - Admin Server:    http://localhost:3035"
    echo "  - Customer Portal: http://localhost:3035/portal"
    echo "  - License API:     http://localhost:3034"
    echo ""

    if [ "$PUSH" = "true" ]; then
        echo "Pushing to registry..."
        docker push owl-license-server:latest
    fi
else
    echo ""
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
