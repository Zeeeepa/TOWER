# Web2API - Universal API Gateway

**Convert any web-based chat interface into an OpenAI-compatible API endpoint.**

## How It Works

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           Web2API Flow                                    │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  1. User provides URL + credentials                                      │
│          ↓                                                               │
│  2. System creates OpenAI API-compatible server                          │
│          ↓                                                               │
│  3. On request: Opens headless browser → navigates to URL                │
│          ↓                                                               │
│  4. Loads cookies from past session (if not first time)                  │
│          ↓                                                               │
│  5. If first time: Performs login sequence → saves cookies               │
│          ↓                                                               │
│  6. Types message into text input field                                  │
│          ↓                                                               │
│  7. Clicks send button                                                   │
│          ↓                                                               │
│  8. Tracks send button state (disabled → enabled = response complete)    │
│          ↓                                                               │
│  9. Extracts response text → formats as OpenAI response → returns        │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- Node.js >= 18
- Python >= 3.12
- Docker & Docker Compose
- PostgreSQL (or use the provided Docker setup)

### Installation

```bash
# Clone and navigate to Web2API
cd backend/web2api

# Run the complete setup (this does everything!)
npm run setup
```

The setup script will:
1. ✅ Check all prerequisites
2. ✅ Create `.env` file with encryption key
3. ✅ Install Python dependencies
4. ✅ Install Node.js dependencies
5. ✅ Initialize PostgreSQL database
6. ✅ Build and start Owl-Browser (may take 10+ minutes)
7. ✅ Verify all services are running

### Development

```bash
# Start everything (frontend + backend)
npm run dev

# Or start separately:
npm run dev:backend   # Python FastAPI on port 8000
npm run dev:frontend  # React Vite on port 5173
```

### Access Points

- **Frontend**: http://localhost:5173
- **Backend API**: http://localhost:8000
- **API Documentation**: http://localhost:8000/docs
- **Owl-Browser Panel**: http://localhost:80

## Usage

### 1. Create a Service via UI

1. Open http://localhost:5173
2. Click "Add Service"
3. Enter service details:
   - Name: `k2think`
   - URL: `https://www.k2think.ai`
   - Email: `developer@pixelium.uk`
   - Password: `developer123?`
4. Click "Create Service"

### 2. Send OpenAI-Compatible API Request

```bash
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "SERVICE_ID_FROM_UI",
    "messages": [
      {"role": "user", "content": "What is 2+2?"}
    ]
  }'
```

**Response:**
```json
{
  "id": "chatcmpl-abc123",
  "object": "chat.completion",
  "created": 1234567890,
  "model": "SERVICE_ID",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "The sum of 2 and 2 is 4."
    },
    "finish_reason": "stop"
  }],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 5,
    "total_tokens": 15
  }
}
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                       Web2API Gateway                        │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌─────────────┐     ┌──────────────┐     ┌─────────────┐ │
│  │  React UI   │────▶│  FastAPI     │────▶│  Owl-Browser│ │
│  │  (Vite)     │     │  Backend     │     │  (Playwright)│ │
│  │  Port 5173  │     │  Port 8000   │     │  Port 8090   │ │
│  └─────────────┘     └──────────────┘     └─────────────┘ │
│                              │                               │
│                              ▼                               │
│                     ┌──────────────┐                        │
│                     │ PostgreSQL   │                        │
│                     │ Port 5432    │                        │
│                     └──────────────┘                        │
└─────────────────────────────────────────────────────────────┘
```

## API Endpoints

### Service Management

- `POST /api/v1/services` - Create new service
- `GET /api/v1/services` - List all services
- `GET /api/v1/services/{id}` - Get service details
- `DELETE /api/v1/services/{id}` - Delete service

### Credentials

- `POST /api/v1/services/{id}/credentials` - Store service credentials
- `GET /api/v1/services/{id}/credentials` - Get service credentials

### Discovery

- `POST /api/v1/services/{id}/discover` - Run service discovery
- `POST /api/v1/services/{id}/login` - Execute login flow

### OpenAI-Compatible API

- `POST /v1/chat/completions` - Send chat completion request
- `GET /v1/models` - List available services as models

### WebSocket

- `WS /ws/services/{id}` - Real-time service updates

## Environment Variables

```bash
# Database
DATABASE_URL=postgresql+asyncpg://user:pass@localhost:5432/web2api

# Owl-Browser
OWL_BROWSER_URL=ws://localhost:8090
OWL_BROWSER_TOKEN=your-token

# Encryption
CREDENTIAL_ENCRYPTION_KEY=your-encryption-key

# API
CORS_ORIGINS=*
API_HOST=0.0.0.0
API_PORT=8000

# Optional: Redis for AutoQA
# REDIS_URL=redis://localhost:6379

# Anthropic AI (for automated testing)
export ANTHROPIC_AUTH_TOKEN=b55b1444d18642059abefbbc7dc8ebe2.G7Zuz4CGmSbkvxyJ
export ANTHROPIC_BASE_URL=https://api.z.ai/api/anthropic
export MODEL=glm-4.7v
```

## Testing

```bash
# Run E2E tests with Playwright
npm run test

# Or run manually
cd autoqa-ai-testing
python -m pytest tests/ -v
```

## Troubleshooting

### Owl-Browser not starting

```bash
# Check logs
cd backend/packages/owl-browser/docker
docker-compose logs -f

# Rebuild
docker-compose build
docker-compose up -d
```

### Database connection errors

```bash
# Start PostgreSQL
cd autoqa-ai-testing
docker-compose up -d postgres

# Run migrations
python -m alembic upgrade head
```

### Backend import errors

```bash
# Install in development mode
cd autoqa-ai-testing
pip install -e .
```

## Production Deployment

```bash
# Build frontend
npm run build:frontend

# Start backend with production server
npm run start:backend

# Or use Docker
docker-compose -f docker-compose.prod.yml up -d
```

## Support

For issues or questions:
- GitHub Issues: https://github.com/Zeeeepa/TOWER/issues
- Documentation: See IMPLEMENTATION_SUMMARY.md
- Testing Guide: See autoqa-ai-testing/tests/README.md

---

**Key Features:**
- ✅ OpenAI-compatible API (`/v1/chat/completions`, `/v1/models`)
- ✅ Automatic cookie persistence for session management
- ✅ Auto-discovery of chat UI elements (input, send button, output)
- ✅ Send button state tracking for response completion detection
- ✅ Streaming response support (SSE)
- ✅ Encrypted credential storage
- ✅ Multi-service support with isolated browser contexts


Why the system created new identificationary logic when there were element_classifier.py and page_analyzer.py and visual_analyzer.py to fully confirm service's functions. Also it has intelligent_crawler.py and state_manager.py furthermore - there is flow_detector.py and form_analyzer.py as well as api_detector.py it should effectively use yaml_builder.py to build service flows and save them. - properly use test_strategy to test all functions of service - like changing model example - clicking on element [model] + selecting model name in a dropdown menu / or if service has toogle functionality [Enable Search ON/OFF] - it should create yaml to enable/disable internet then. of course it should properly identify all functions of the service first, then identify all flows and actions needed to use these services. and then to save yamls to programically manage all functions of service. test_builder.py should be adjusted to build test flows for these functions of service only, not whole page's testing. orchestrator also should be adjusted for specific logic of service flow discovery, testing, flow yaml building. from llm_enhanced it should use generate_login_steps_llm as it inherits login required logic. Config_generator should be more properly integrated with existant functionalities. feature_mapper should have fallback mechanics to vision models and ai reasoning functions to intelligently handle edge cases. operation builder to be propriatel upgraded too as well as discovery orchestrator too. in should properly use dsl features for more advanced integration and effectiveness. for stealth_config.py it should instead properly use /home/l/TOWER/packages/owl-browser/data/profiles/profiles.sql
/home/l/TOWER/packages/owl-browser/data/profiles/schema.sql
/home/l/TOWER/packages/owl-browser/data/profiles/validate_profiles.py
/home/l/TOWER/packages/owl-browser/data/profiles/README.md


Also- it should properly build /home/l/TOWER/packages/owl-browser/BUILD_OPTIONS.md
/home/l/TOWER/packages/owl-browser/BUILD-GUIDE.md
/home/l/TOWER/packages/owl-browser/README.md
Building either proper http-server or docker server [preferable building proper docker /home/l/TOWER/packages/owl-browser/docker/README.md

with properly upgraded and adjusted /home/l/TOWER/packages/owl-browser/docker/entrypoint.sh to include LOCAL verification rather than using service's online verification 
also to adjust /home/l/TOWER/packages/owl-browser/docker/Dockerfile  to upgrade license server url not to be needed at all. or if applicable -> to modify /home/l/TOWER/packages/owl-browser/license-server to be fully configured and launched to match docker deployment with LOCAL CONFIGURATION not online 


Furthermore -> fully effectively use test_runner.py to test all flow steps of service functions. as well as using self_healing.py to increase effectiveness. /home/l/TOWER/backend/src/runner/self_healing.py

properly use /home/l/TOWER/backend/src/orchestrator/scheduler.py
to in accordance effectively discover, test, build service function flow actions for progrmaical access. Effectively upgrade service_models.py to match all requirements.  Properly to use ML_Engline to learn from each attempt. As well as its engine /home/l/TOWER/backend/src/assertions/engine.py

aand fully upgrade /home/l/TOWER/backend/src/api/main.py to merge and unify with /home/l/TOWER/backend/src/api/main_new.py

ll that to reflect on /home/l/TOWER/backend/src/__init__.py
and effectively be implemented in /home/l/TOWER/backend/src/cli.py  so that frontend could effectively make calls to cli to effectively transform any website [With ai chat interfaces] to openai api compatible endpoints for inference programically  as well as discovering all features/functions to be able to configure them programically too. 