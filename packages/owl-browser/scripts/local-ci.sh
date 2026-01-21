#!/bin/bash
# =============================================================================
# Local CI Script for Owl Browser
# =============================================================================
# Run this before pushing to catch issues early.
#
# Usage:
#   ./scripts/local-ci.sh           # Run all checks
#   ./scripts/local-ci.sh quick     # Quick checks only (no build)
#   ./scripts/local-ci.sh build     # Build checks only
#   ./scripts/local-ci.sh test      # Test checks only
#   ./scripts/local-ci.sh sdk       # SDK checks only
#   ./scripts/local-ci.sh python    # Python SDK checks only
# =============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Counters
PASSED=0
FAILED=0
SKIPPED=0

# Helper functions
print_header() {
    echo ""
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}  $1${NC}"
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════${NC}"
}

print_step() {
    echo -e "\n${CYAN}▶ $1${NC}"
}

print_pass() {
    echo -e "${GREEN}✓ $1${NC}"
    PASSED=$((PASSED + 1))
}

print_fail() {
    echo -e "${RED}✗ $1${NC}"
    FAILED=$((FAILED + 1))
}

print_skip() {
    echo -e "${YELLOW}○ $1 (skipped)${NC}"
    SKIPPED=$((SKIPPED + 1))
}

# Check if command exists
has_command() {
    command -v "$1" >/dev/null 2>&1
}

# =============================================================================
# Checks
# =============================================================================

check_yaml() {
    print_step "Validating YAML files..."
    if has_command python3; then
        for f in .github/workflows/*.yml .github/dependabot.yml; do
            if python3 -c "import yaml; yaml.safe_load(open('$f'))" 2>/dev/null; then
                echo "  ✓ $f"
            else
                echo "  ✗ $f"
                return 1
            fi
        done
        print_pass "YAML validation"
    else
        print_skip "YAML validation (python3 not found)"
    fi
}

check_actionlint() {
    print_step "Validating GitHub Actions workflows..."
    if has_command actionlint; then
        if actionlint .github/workflows/*.yml 2>&1; then
            print_pass "actionlint"
        else
            print_fail "actionlint"
            return 1
        fi
    else
        print_skip "actionlint (not installed: brew install actionlint)"
    fi
}

check_json() {
    print_step "Validating JSON files..."
    local failed=0
    for f in package.json sdk/package.json tests/ci-config.json; do
        if [ -f "$f" ]; then
            if node -e "JSON.parse(require('fs').readFileSync('$f'))" 2>/dev/null; then
                echo "  ✓ $f"
            else
                echo "  ✗ $f"
                failed=1
            fi
        fi
    done
    if [ $failed -eq 0 ]; then
        print_pass "JSON validation"
    else
        print_fail "JSON validation"
        return 1
    fi
}

check_typescript_sdk() {
    print_step "Checking TypeScript SDK..."
    if [ -d "sdk" ] && [ -f "sdk/package.json" ]; then
        if [ -d "sdk/node_modules" ]; then
            if (cd sdk && npm run build 2>/dev/null); then
                print_pass "TypeScript SDK build"
            else
                print_fail "TypeScript SDK build"
                return 1
            fi
        else
            print_skip "TypeScript SDK (run: cd sdk && npm install)"
        fi
    else
        print_skip "TypeScript SDK (not found)"
    fi
}

check_python_sdk() {
    print_step "Checking Python SDK..."
    if [ -d "python-sdk" ] && [ -f "python-sdk/pyproject.toml" ]; then
        local failed=0

        # Check ruff
        if has_command ruff; then
            echo "  Running ruff..."
            if (cd python-sdk && ruff check owl_browser/ 2>/dev/null); then
                echo "  ✓ ruff"
            else
                echo "  ✗ ruff"
                failed=1
            fi
        else
            echo "  ○ ruff (not installed: pip install ruff)"
        fi

        # Check mypy
        if has_command mypy; then
            echo "  Running mypy..."
            if (cd python-sdk && mypy owl_browser/ --ignore-missing-imports 2>/dev/null); then
                echo "  ✓ mypy"
            else
                echo "  ✗ mypy"
                failed=1
            fi
        else
            echo "  ○ mypy (not installed: pip install mypy)"
        fi

        # Check black
        if has_command black; then
            echo "  Running black..."
            if (cd python-sdk && black --check owl_browser/ 2>/dev/null); then
                echo "  ✓ black"
            else
                echo "  ✗ black (run: cd python-sdk && black owl_browser/)"
                failed=1
            fi
        else
            echo "  ○ black (not installed: pip install black)"
        fi

        if [ $failed -eq 0 ]; then
            print_pass "Python SDK checks"
        else
            print_fail "Python SDK checks"
            return 1
        fi
    else
        print_skip "Python SDK (not found)"
    fi
}

check_build() {
    print_step "Checking C++ build..."
    if [ -d "build" ] && [ -f "build/owl_browser" -o -d "build/Release" ]; then
        print_pass "Build exists"
    else
        print_skip "Build not found (run: npm run build)"
    fi
}

check_tests() {
    print_step "Running test suite list..."
    if [ -f "scripts/run-ci-tests.cjs" ]; then
        node scripts/run-ci-tests.cjs --list 2>/dev/null | head -20
        print_pass "Test runner available"
    else
        print_skip "Test runner not found"
    fi
}

check_pre_commit() {
    print_step "Running pre-commit hooks..."
    if has_command pre-commit; then
        if [ -f ".pre-commit-config.yaml" ]; then
            if pre-commit run --all-files 2>/dev/null; then
                print_pass "pre-commit hooks"
            else
                print_fail "pre-commit hooks"
                return 1
            fi
        else
            print_skip "pre-commit config not found"
        fi
    else
        print_skip "pre-commit (not installed: pip install pre-commit)"
    fi
}

# =============================================================================
# Main
# =============================================================================

MODE="${1:-all}"

print_header "Owl Browser Local CI"
echo -e "Mode: ${BOLD}$MODE${NC}"
echo "Date: $(date)"

case "$MODE" in
    quick)
        check_yaml
        check_actionlint
        check_json
        ;;
    build)
        check_build
        check_typescript_sdk
        check_python_sdk
        ;;
    test)
        check_tests
        ;;
    sdk)
        check_typescript_sdk
        ;;
    python)
        check_python_sdk
        ;;
    pre-commit)
        check_pre_commit
        ;;
    all)
        check_yaml
        check_actionlint
        check_json
        check_typescript_sdk
        check_python_sdk
        check_build
        check_tests
        ;;
    *)
        echo "Usage: $0 [quick|build|test|sdk|python|pre-commit|all]"
        exit 1
        ;;
esac

# Summary
print_header "Summary"
echo -e "  ${GREEN}Passed:  $PASSED${NC}"
echo -e "  ${RED}Failed:  $FAILED${NC}"
echo -e "  ${YELLOW}Skipped: $SKIPPED${NC}"

if [ $FAILED -gt 0 ]; then
    echo -e "\n${RED}${BOLD}Local CI failed!${NC} Fix the issues above before pushing."
    exit 1
else
    echo -e "\n${GREEN}${BOLD}Local CI passed!${NC} Ready to push."
    exit 0
fi
