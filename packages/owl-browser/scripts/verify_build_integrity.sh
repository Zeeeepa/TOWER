#!/bin/bash
#
# Build Integrity Verification Script
# ====================================
# Verifies that the compiled browser binary:
#   1. Has symbols properly stripped
#   2. Has no extractable public key
#   3. Is properly code signed (if signing is configured)
#   4. Has correct security flags
#   5. Matches expected checksums (if baseline exists)
#
# Usage:
#   ./scripts/verify_build_integrity.sh              # Verify headless build
#   ./scripts/verify_build_integrity.sh --ui         # Verify UI build
#   ./scripts/verify_build_integrity.sh --all        # Verify both builds
#   ./scripts/verify_build_integrity.sh --generate   # Generate baseline checksums
#
# Exit codes:
#   0 - All checks passed
#   1 - One or more checks failed
#   2 - Binary not found
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build/Release"
CHECKSUM_FILE="$PROJECT_ROOT/.build_checksums"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse arguments
CHECK_HEADLESS=false
CHECK_UI=false
GENERATE_BASELINE=false
VERBOSE=false

for arg in "$@"; do
    case $arg in
        --ui)
            CHECK_UI=true
            ;;
        --headless)
            CHECK_HEADLESS=true
            ;;
        --all)
            CHECK_HEADLESS=true
            CHECK_UI=true
            ;;
        --generate)
            GENERATE_BASELINE=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --help|-h)
            echo "Build Integrity Verification"
            echo ""
            echo "Usage:"
            echo "  $0                 Verify headless build (default)"
            echo "  $0 --ui            Verify UI build"
            echo "  $0 --all           Verify both builds"
            echo "  $0 --generate      Generate baseline checksums"
            echo "  $0 --verbose       Show detailed output"
            echo ""
            echo "This script verifies:"
            echo "  - Symbols are stripped (no license function names visible)"
            echo "  - Public key is obfuscated (not extractable via strings)"
            echo "  - Code signing status"
            echo "  - Binary checksums match baseline (if available)"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $arg${NC}"
            exit 1
            ;;
    esac
done

# Default to headless if nothing specified
if ! $CHECK_HEADLESS && ! $CHECK_UI && ! $GENERATE_BASELINE; then
    CHECK_HEADLESS=true
fi

FAILED=0
PASSED=0
WARNINGS=0

log_pass() {
    echo -e "  ${GREEN}✓${NC} $1"
    ((PASSED++))
}

log_fail() {
    echo -e "  ${RED}✗${NC} $1"
    ((FAILED++))
}

log_warn() {
    echo -e "  ${YELLOW}⚠${NC} $1"
    ((WARNINGS++))
}

log_info() {
    echo -e "  ${BLUE}ℹ${NC} $1"
}

# Generate SHA256 checksum of binary
get_checksum() {
    local binary="$1"
    shasum -a 256 "$binary" 2>/dev/null | cut -d' ' -f1
}

# Verify a single binary
verify_binary() {
    local name="$1"
    local binary_path="$2"
    local app_path="$3"

    echo ""
    echo -e "${BLUE}=== Verifying: $name ===${NC}"
    echo ""

    # Check if binary exists
    if [ ! -f "$binary_path" ]; then
        log_fail "Binary not found: $binary_path"
        echo -e "${YELLOW}Run 'npm run build' first${NC}"
        return 1
    fi
    log_pass "Binary exists"

    # 1. Check symbols are stripped
    echo ""
    echo "Checking symbol stripping..."
    local license_symbols=$(nm "$binary_path" 2>&1 | grep -iE "LicenseManager|ValidateLicense|CheckLicense" | wc -l | tr -d ' ')
    if [ "$license_symbols" = "0" ]; then
        log_pass "License symbols stripped"
    else
        log_fail "License symbols exposed ($license_symbols found)"
        if $VERBOSE; then
            nm "$binary_path" 2>&1 | grep -iE "LicenseManager|ValidateLicense|CheckLicense" | head -5
        fi
    fi

    # 2. Check public key is not extractable
    echo ""
    echo "Checking key obfuscation..."
    local key_visible=$(strings "$binary_path" 2>/dev/null | grep -E "BEGIN PUBLIC KEY|BEGIN RSA|MIIBIjAN" | wc -l | tr -d ' ')
    if [ "$key_visible" = "0" ]; then
        log_pass "Public key obfuscated"
    else
        log_fail "Public key extractable via strings"
    fi

    # 3. Check revealing error messages
    echo ""
    echo "Checking error message obfuscation..."
    local revealing_msgs=$(strings "$binary_path" 2>/dev/null | grep -E "License validated successfully|Invalid license magic|Invalid license signature" | wc -l | tr -d ' ')
    if [ "$revealing_msgs" = "0" ]; then
        log_pass "Internal messages obfuscated"
    else
        log_warn "Some revealing messages found ($revealing_msgs)"
    fi

    # 4. Check code signing
    echo ""
    echo "Checking code signing..."
    local sign_info=$(codesign -dv "$app_path" 2>&1)
    local sign_type=$(echo "$sign_info" | grep "Signature=" | cut -d= -f2)

    if echo "$sign_info" | grep -q "Signature=adhoc"; then
        log_warn "Ad-hoc signed (production requires Apple Developer certificate)"
    elif echo "$sign_info" | grep -q "Authority=Developer ID"; then
        log_pass "Signed with Developer ID certificate"
        local team_id=$(echo "$sign_info" | grep "TeamIdentifier=" | cut -d= -f2)
        log_info "Team ID: $team_id"
    elif echo "$sign_info" | grep -q "Authority=Apple"; then
        log_pass "Signed with Apple certificate"
    else
        log_warn "Unknown signing status: $sign_type"
    fi

    # 5. Check binary architecture
    echo ""
    echo "Checking binary architecture..."
    local arch=$(file "$binary_path" | grep -o "arm64\|x86_64" | head -1)
    if [ -n "$arch" ]; then
        log_pass "Architecture: $arch"
    else
        log_warn "Could not determine architecture"
    fi

    # 6. Check for debug symbols
    echo ""
    echo "Checking for debug info..."
    local has_debug=$(dwarfdump "$binary_path" 2>/dev/null | head -5 | wc -l | tr -d ' ')
    if [ "$has_debug" = "0" ]; then
        log_pass "No debug symbols (DWARF)"
    else
        log_warn "Debug symbols present"
    fi

    # 7. Verify checksum if baseline exists
    echo ""
    echo "Checking binary checksum..."
    local current_checksum=$(get_checksum "$binary_path")

    if [ -f "$CHECKSUM_FILE" ]; then
        local baseline_checksum=$(grep "^$name:" "$CHECKSUM_FILE" 2>/dev/null | cut -d: -f2)
        if [ -n "$baseline_checksum" ]; then
            if [ "$current_checksum" = "$baseline_checksum" ]; then
                log_pass "Checksum matches baseline"
            else
                log_warn "Checksum differs from baseline (binary was rebuilt)"
                if $VERBOSE; then
                    echo "    Current:  $current_checksum"
                    echo "    Baseline: $baseline_checksum"
                fi
            fi
        else
            log_info "No baseline checksum for $name"
        fi
    else
        log_info "No baseline file (run with --generate to create)"
    fi

    # 8. Check binary size (sanity check)
    echo ""
    echo "Checking binary size..."
    local size=$(stat -f%z "$binary_path" 2>/dev/null || stat -c%s "$binary_path" 2>/dev/null)
    local size_mb=$((size / 1024 / 1024))
    if [ "$size_mb" -gt 0 ] && [ "$size_mb" -lt 500 ]; then
        log_pass "Binary size: ${size_mb}MB (reasonable)"
    elif [ "$size_mb" -ge 500 ]; then
        log_warn "Binary size: ${size_mb}MB (unusually large)"
    else
        log_warn "Binary size: ${size} bytes"
    fi

    echo ""
    echo "Current SHA256: $current_checksum"

    return 0
}

# Generate baseline checksums
generate_baseline() {
    echo -e "${BLUE}=== Generating Baseline Checksums ===${NC}"
    echo ""

    local headless_binary="$BUILD_DIR/owl_browser.app/Contents/MacOS/owl_browser"
    local ui_binary="$BUILD_DIR/owl_browser_ui.app/Contents/MacOS/owl_browser_ui"

    # Clear existing file
    echo "# Build checksums - generated $(date)" > "$CHECKSUM_FILE"
    echo "# Regenerate with: npm run build:verify -- --generate" >> "$CHECKSUM_FILE"
    echo "" >> "$CHECKSUM_FILE"

    if [ -f "$headless_binary" ]; then
        local checksum=$(get_checksum "$headless_binary")
        echo "owl_browser:$checksum" >> "$CHECKSUM_FILE"
        log_pass "owl_browser: $checksum"
    else
        log_warn "Headless binary not found"
    fi

    if [ -f "$ui_binary" ]; then
        local checksum=$(get_checksum "$ui_binary")
        echo "owl_browser_ui:$checksum" >> "$CHECKSUM_FILE"
        log_pass "owl_browser_ui: $checksum"
    else
        log_warn "UI binary not found"
    fi

    echo ""
    echo -e "${GREEN}Baseline saved to: $CHECKSUM_FILE${NC}"
    echo ""
    echo -e "${YELLOW}Note: Add this file to .gitignore if you don't want to track it${NC}"
    echo -e "${YELLOW}      or commit it to verify builds match across machines${NC}"
}

# Main execution
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}   Build Integrity Verification${NC}"
echo -e "${BLUE}========================================${NC}"

if $GENERATE_BASELINE; then
    generate_baseline
    exit 0
fi

if $CHECK_HEADLESS; then
    verify_binary "owl_browser" \
        "$BUILD_DIR/owl_browser.app/Contents/MacOS/owl_browser" \
        "$BUILD_DIR/owl_browser.app"
fi

if $CHECK_UI; then
    verify_binary "owl_browser_ui" \
        "$BUILD_DIR/owl_browser_ui.app/Contents/MacOS/owl_browser_ui" \
        "$BUILD_DIR/owl_browser_ui.app"
fi

# Summary
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}   Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "  ${GREEN}Passed:${NC}   $PASSED"
echo -e "  ${YELLOW}Warnings:${NC} $WARNINGS"
echo -e "  ${RED}Failed:${NC}   $FAILED"
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}BUILD INTEGRITY CHECK FAILED${NC}"
    echo ""
    echo "Security issues detected. Please review and fix before distribution."
    exit 1
elif [ $WARNINGS -gt 0 ]; then
    echo -e "${YELLOW}BUILD INTEGRITY CHECK PASSED WITH WARNINGS${NC}"
    echo ""
    echo "Consider addressing warnings for production builds."
    exit 0
else
    echo -e "${GREEN}BUILD INTEGRITY CHECK PASSED${NC}"
    exit 0
fi
