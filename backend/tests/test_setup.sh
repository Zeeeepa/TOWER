#!/usr/bin/env bash
# Test setup script for TOWER Web2API with Z.AI and k2think
# This script sets up the environment and verifies the integration

set -e

echo "=========================================="
echo "TOWER Web2API Test Setup"
echo "=========================================="

# Change to backend directory
cd "$(dirname "$0")"

# Environment variables for testing
export AUTOQA_LLM_ENABLED=true
export AUTOQA_LLM_BASE_URL="https://api.z.ai/api/anthropic"
export AUTOQA_LLM_API_KEY="b55b1444d18642059abefbbc7dc8ebe2.G7Zuz4CGmSbkvxyJ"
export AUTOQA_LLM_MODEL="glm-4.7v"
export AUTOQA_LLM_PROVIDER="anthropic"
export AUTOQA_LLM_TEST_BUILDER_ENABLED=true
export AUTOQA_LLM_STEP_TRANSFORMER_ENABLED=true
export AUTOQA_LLM_ASSERTIONS_ENABLED=true
export AUTOQA_LLM_SELF_HEALING_ENABLED=true
export AUTOQA_LLM_CONFIG_FILE="llm-config.yaml"

# K2Think test credentials
export K2THINK_URL="https://www.k2think.ai"
export K2THINK_EMAIL="developer@pixelium.uk"
export K2THINK_PASSWORD="developer123?"

# Database setup (use SQLite for testing)
export DATABASE_URL="sqlite+aiosqlite:///./test_web2api.db"

# Redis (optional, for queue)
# export REDIS_URL="redis://localhost:6379"

echo ""
echo "1. Checking Python environment..."
python3 --version

echo ""
echo "2. Installing owl-browser SDK from local source..."
pip install -e ../packages/owl-browser/python-sdk/ --quiet

echo ""
echo "3. Installing backend dependencies..."
pip install -e . --quiet

echo ""
echo "4. Verifying owl-browser import..."
python3 -c "from owl_browser import Browser, RemoteConfig; print('✅ owl-browser SDK imported successfully')"

echo ""
echo "5. Verifying LLM configuration..."
python3 << 'EOF'
from autoqa.llm.config import load_llm_config
config = load_llm_config("llm-config.yaml")
print(f"✅ LLM enabled: {config.enabled}")
print(f"   Model: {config.default_endpoint.model}")
print(f"   Provider: {config.default_endpoint.provider}")
print(f"   Base URL: {config.default_endpoint.base_url}")
print(f"   Test Builder: {config.test_builder.enabled}")
print(f"   Self-healing: {config.self_healing.enabled}")
EOF

echo ""
echo "6. Testing Z.AI API connectivity..."
python3 << 'EOF'
import httpx
import os
import json

url = "https://api.z.ai/api/anthropic/v1/messages"
api_key = "b55b1444d18642059abefbbc7dc8ebe2.G7Zuz4CGmSbkvxyJ"

headers = {
    "Content-Type": "application/json",
    "x-api-key": api_key,
    "anthropic-version": "2023-06-01"
}

data = {
    "model": "glm-4.7v",
    "max_tokens": 100,
    "messages": [
        {"role": "user", "content": "Hello! Please respond with 'API test successful'."}
    ]
}

try:
    response = httpx.post(url, headers=headers, json=data, timeout=30)
    if response.status_code == 200:
        result = response.json()
        content = result.get("content", [{}])[0].get("text", "")
        print(f"✅ Z.AI API responded: {content[:100]}...")
    else:
        print(f"⚠️  Z.AI API returned status {response.status_code}: {response.text[:200]}")
except Exception as e:
    print(f"❌ Z.AI API error: {e}")
EOF

echo ""
echo "=========================================="
echo "Setup complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. To run with local owl-browser (requires browser binary):"
echo "   export OWL_BROWSER_PATH=/path/to/owl_browser"
echo "   autoqa-server"
echo ""
echo "2. To run with remote owl-browser server:"
echo "   export OWL_BROWSER_URL=http://localhost:8080"
echo "   export OWL_BROWSER_TOKEN=your-token"
echo "   autoqa-server"
echo ""
echo "3. To run integration tests:"
echo "   pytest tests/test_k2think_integration.py -v"
echo ""
