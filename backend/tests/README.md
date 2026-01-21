# Web2API Testing Guide

Complete testing guide for the Web2API integration with Owl-Browser backend.

## Overview

This testing suite validates:
1. ✅ Static analysis fixes (TYPE_CHECKING, path traversal, error messages)
2. ✅ Owl-Browser backend connectivity
3. ✅ Service creation and credential management
4. ✅ Login flow execution
5. ✅ Text input and button identification
6. ✅ Send button state tracking
7. ✅ Response retrieval and formatting
8. ✅ OpenAI API compatibility
9. ✅ Advanced stealth features

## Prerequisites

```bash
# Required services
- PostgreSQL database running
- Owl-Browser service running
- Web2API backend running
- Python 3.12+
```

## Setup

### 1. Environment Variables

```bash
export DATABASE_URL="postgresql+asyncpg://user:pass@host/db"
export OWL_BROWSER_URL="ws://owl-browser:8090"
export OWL_BROWSER_TOKEN="your-token"
export CREDENTIAL_ENCRYPTION_KEY=$(python -c "from cryptography.fernet import Fernet; print(Fernet.generate_key().decode())")
export WEB2API_BASE_URL="http://localhost:8000"
```

### 2. Install Test Dependencies

```bash
cd backend/web2api/autoqa-ai-testing
pip install pytest pytest-asyncio httpx playwright structlog
```

### 3. Install Playwright Browsers

```bash
playwright install chromium
```

## Test Suites

### 1. Static Analysis Tests

These tests verify that all static analysis issues are fixed.

```bash
# Run static analysis
pytest tests/test_static_analysis.py -v
```

**Coverage:**
- ✅ TYPE_CHECKING import fixes
- ✅ Path traversal vulnerability fixes
- ✅ Error message exposure fixes
- ✅ UUID validation fixes
- ✅ Unused import removals

### 2. Owl-Browser Integration Tests

Comprehensive tests for Owl-Browser backend integration.

```bash
# Run integration tests
pytest tests/test_owl_browser_integration.py -v
```

**Coverage:**
- ✅ Service creation via REST API
- ✅ Service credential storage
- ✅ Browser connection verification
- ✅ Text input identification
- ✅ Send button identification
- ✅ Send button state tracking
- ✅ Text input interaction
- ✅ Send button clicking
- ✅ Response waiting
- ✅ Response text retrieval
- ✅ OpenAI format response
- ✅ Complete end-to-end workflow

### 3. K2Think Service Integration Tests

Specific tests for K2Think service integration.

```bash
# Run K2Think integration tests
pytest tests/test_k2think_integration.py -v

# Or run directly
python tests/test_k2think_integration.py
```

**Coverage:**
- ✅ Service creation with K2Think credentials
- ✅ Credential storage (email: developer@pixelium.uk)
- ✅ Service discovery
- ✅ Login flow execution
- ✅ Element identification (text input, send button)
- ✅ Message sending via Web2API
- ✅ Response format verification
- ✅ Multi-turn conversations
- ✅ Error handling

### 4. Stealth Features Tests

Tests for advanced anti-detection features.

```bash
# Run stealth tests
pytest tests/test_owl_browser_integration.py::TestAdvancedStealthFeatures -v
```

**Coverage:**
- ✅ WebDriver fingerprinting protection
- ✅ Canvas fingerprinting protection
- ✅ Browser headers normalization
- ✅ User agent spoofing
- ✅ Geolocation spoofing
- ✅ Timezone spoofing
- ✅ Language spoofing
- ✅ Screen resolution spoofing

## Manual Testing

### Test 1: Service Creation

```bash
curl -X POST http://localhost:8000/api/v1/services \
  -H "Content-Type: application/json" \
  -d '{
    "name": "k2think",
    "url": "https://www.k2think.ai",
    "description": "K2Think AI Service"
  }'
```

### Test 2: Store Credentials

```bash
# Replace SERVICE_ID with actual ID from test 1
curl -X POST http://localhost:8000/api/v1/services/SERVICE_ID/credentials \
  -H "Content-Type: application/json" \
  -d '{
    "email": "developer@pixelium.uk",
    "password": "developer123?"
  }'
```

### Test 3: Send OpenAI-Compatible Request

```bash
# Replace SERVICE_ID with actual ID
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "SERVICE_ID",
    "messages": [
      {"role": "user", "content": "What is 2+2?"}
    ],
    "temperature": 0.7,
    "max_tokens": 1000
  }'
```

### Test 4: List Services

```bash
curl http://localhost:8000/v1/models
```

## Expected Results

### OpenAI-Compatible Response

```json
{
  "id": "chatcmpl-abc123",
  "object": "chat.completion",
  "created": 1234567890,
  "model": "SERVICE_ID",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "2+2 equals 4."
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 5,
    "total_tokens": 15
  }
}
```

## Troubleshooting

### Issue: Service Not Ready

**Error:** `503 Service Unavailable - Service not ready`

**Solution:**
1. Check service discovery completed: `GET /api/v1/services/SERVICE_ID`
2. Verify login succeeded: Check logs for authentication errors
3. Ensure Owl-Browser is connected

### Issue: WebDriver Detected

**Error:** Bot detection triggers

**Solution:**
1. Verify stealth scripts are applied
2. Check user agent is set correctly
3. Ensure canvas protection is enabled
4. Verify all navigator properties are masked

### Issue: Text Input Not Found

**Error:** Element identification fails

**Service:**
1. Run service discovery: `POST /api/v1/services/SERVICE_ID/discover`
2. Check discovery results for element selectors
3. Verify page structure hasn't changed
4. Use AI-powered discovery as fallback

### Issue: Send Button State Not Updating

**Error:** Button state stuck on "disabled"

**Solution:**
1. Verify page has fully loaded
2. Check for JavaScript errors
3. Ensure text has been entered
4. Wait for page to process input

## Performance Benchmarks

Expected response times:

| Operation | Target Time | Max Time |
|-----------|-------------|----------|
| Service Creation | < 100ms | 500ms |
| Credential Storage | < 50ms | 200ms |
| Service Discovery | 5-10s | 30s |
| Login Flow | 3-5s | 15s |
| Message Send | < 1s | 3s |
| Response Retrieval | 5-10s | 30s |

## Continuous Integration

### GitHub Actions Workflow

```yaml
name: Web2API Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest

    services:
      postgres:
        image: postgres:15
        env:
          POSTGRES_PASSWORD: test
        options: >-
          --health-cmd pg_isready
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

      owl-browser:
        image: owl-browser:latest
        options: >-
          --health-cmd "curl -f http://localhost:8090/health || exit 1"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-python@v4
        with:
          python-version: '3.12'

      - name: Install dependencies
        run: |
          cd backend/web2api/autoqa-ai-testing
          pip install -r requirements.txt
          pip install pytest pytest-asyncio httpx playwright

      - name: Install Playwright browsers
        run: playwright install chromium

      - name: Run static analysis tests
        run: pytest tests/test_static_analysis.py -v

      - name: Run integration tests
        run: pytest tests/test_owl_browser_integration.py -v

      - name: Run K2Think integration tests
        run: pytest tests/test_k2think_integration.py -v
        env:
          DATABASE_URL: postgresql+asyncpg://postgres:test@localhost:5432/test
          OWL_BROWSER_URL: ws://localhost:8090
          OWL_BROWSER_TOKEN: test-token
          CREDENTIAL_ENCRYPTION_KEY: test-key
          WEB2API_BASE_URL: http://localhost:8000
```

## Next Steps

1. ✅ All static analysis issues fixed
2. ✅ Comprehensive test suite created
3. ✅ Advanced stealth features implemented
4. ⏳ Run full test suite
5. ⏳ Fix any failing tests
6. ⏳ Deploy to production
7. ⏳ Monitor performance metrics

## Support

For issues or questions:
- Check logs: `tail -f backend/web2api/autoqa-ai-testing/logs/web2api.log`
- Run diagnostics: `python tests/diagnostics.py`
- Open GitHub issue with error details

---

**Status:** ✅ Ready for Testing
**Last Updated:** 2025-01-20
**Version:** 1.0.0
