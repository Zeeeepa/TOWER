# Web2API - Universal API Gateway

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
export ANTHROPIC_AUTH_TOKEN=your-token
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

**Status**: ✅ Production Ready
**Version**: 1.0.0
**Last Updated**: 2025-01-20
