# Local CI Tools

Run CI checks locally before pushing to catch issues early and save time.

## Quick Start

```bash
# Run all local CI checks
npm run ci:local

# Run quick checks only (YAML, JSON validation)
npm run ci:local:quick

# Run pre-commit hooks
npm run ci:pre-commit
```

---

## Available Tools

### 1. Local CI Script

A comprehensive script that runs the same checks as the GitHub Actions CI.

```bash
# All checks
./scripts/local-ci.sh

# Specific modes
./scripts/local-ci.sh quick    # YAML, JSON, actionlint only
./scripts/local-ci.sh build    # Build verification
./scripts/local-ci.sh sdk      # TypeScript SDK only
./scripts/local-ci.sh python   # Python SDK only
./scripts/local-ci.sh test     # List test suites
```

**What it checks:**
- YAML syntax validation
- GitHub Actions workflow validation (actionlint)
- JSON syntax validation
- TypeScript SDK build
- Python SDK (ruff, mypy, black)
- Build verification

---

### 2. Pre-commit Hooks

Automatic checks that run before each commit.

**Install:**
```bash
# Install pre-commit (if not installed)
pip install pre-commit

# Install hooks in this repo
pre-commit install
```

**Run manually:**
```bash
# Run all hooks on all files
pre-commit run --all-files

# Run specific hook
pre-commit run actionlint --all-files
```

**Hooks included:**
- `trailing-whitespace` - Remove trailing whitespace
- `end-of-file-fixer` - Ensure files end with newline
- `check-yaml` - Validate YAML syntax
- `check-json` - Validate JSON syntax
- `check-merge-conflict` - Check for merge conflict markers
- `actionlint` - Validate GitHub Actions workflows
- `ruff` - Python linting and formatting
- `shellcheck` - Shell script validation

---

### 3. actionlint

Validates GitHub Actions workflow files.

**Install:**
```bash
brew install actionlint
```

**Run:**
```bash
actionlint .github/workflows/*.yml
```

---

### 4. Test Runner

Run CI test suites locally.

```bash
# List available test suites
npm run ci:test:list

# Run specific suite
npm run ci:test:smoke      # Quick smoke tests
npm run ci:test:core       # Core automation tests
npm run ci:test:security   # Security tests
npm run ci:test:all        # All tests
```

---

## npm Scripts Reference

| Script | Description |
|--------|-------------|
| `npm run ci:local` | Run all local CI checks |
| `npm run ci:local:quick` | Quick checks (YAML, JSON, actionlint) |
| `npm run ci:pre-commit` | Run pre-commit hooks |
| `npm run ci:test:list` | List available test suites |
| `npm run ci:test:smoke` | Run smoke tests |
| `npm run ci:test:core` | Run core tests |
| `npm run ci:test:all` | Run all tests |

---

## Recommended Workflow

Before pushing:

```bash
# 1. Quick validation
npm run ci:local:quick

# 2. If you changed SDK code
cd sdk && npm run build

# 3. If you changed Python SDK
cd python-sdk && ruff check owl_browser/ && black --check owl_browser/

# 4. Run smoke tests (if browser is built)
npm run ci:test:smoke
```

Or just run everything:

```bash
npm run ci:local
```

---

## Installing All Tools

```bash
# macOS
brew install actionlint shellcheck

# Python tools
pip install pre-commit ruff black mypy

# Install pre-commit hooks
pre-commit install

# Install Node.js dependencies
npm install
cd sdk && npm install
```

---

## Troubleshooting

### actionlint not found
```bash
brew install actionlint
```

### pre-commit not found
```bash
pip install pre-commit
```

### Python SDK checks failing
```bash
cd python-sdk
pip install -e ".[dev]"
```

### TypeScript SDK checks failing
```bash
cd sdk
npm install
npm run build
```
