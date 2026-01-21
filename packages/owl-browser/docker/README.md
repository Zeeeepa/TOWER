# Owl Browser Docker Server

Docker container for Owl Browser with REST API and web control panel.

## Quick Start

```bash
# Run the container
docker run -d \
  --name owl-browser \
  -p 80:80 \
  -p 8080:8080 \
  -e OWL_HTTP_TOKEN=your-secret-token \
  -e OWL_PANEL_PASSWORD=your-password \
  -v owl-browser-config:/root/.config/owl-browser \
  --shm-size=2g \
  owl-browser:amd64

# Open control panel
open http://localhost

# Check health
curl http://localhost:8080/health
```

## License

The browser requires a valid license. Mount your license volume:

```bash
-v owl-browser-config:/root/.config/owl-browser
```

The volume should contain `license.olic`. The browser looks for:
```
/root/.config/owl-browser/license.olic
```

### License API

```bash
# Check license status
curl -X POST -H "Authorization: Bearer your-token" \
  http://localhost:8080/execute/browser_get_license_status

# Get hardware fingerprint (for new license)
curl -X POST -H "Authorization: Bearer your-token" \
  http://localhost:8080/execute/browser_get_hardware_fingerprint

# Add license via base64
LICENSE=$(base64 -i /path/to/license.olic)
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d "{\"license_content\": \"$LICENSE\"}" \
  http://localhost:8080/execute/browser_add_license
```

## Web Control Panel

The container includes a React-based web control panel served via Nginx on port 80.

### Accessing the Control Panel

```bash
# Open in browser
open http://localhost

# Or with custom port
docker run -p 3000:80 ... owl-browser:amd64
open http://localhost:3000
```

### Features

- **API Playground** (`/playground`) - Interactive tool to test all 100+ browser APIs
- **Live Browser View** - Real-time MJPEG video streaming of browser sessions
- **Test Builder** - Create and export automation test scripts
- **IPC Tests** - End-to-end IPC protocol testing with detailed reports
- **License Management** - View license status and add licenses via UI
- **Documentation** - Built-in API reference and examples

### Authentication

The control panel uses password authentication separate from the API token:

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_PANEL_PASSWORD` | `changeme` | Password to access the control panel |

The API playground requires the API token (`OWL_HTTP_TOKEN`) to execute browser commands.

### Routes

| Route | Description |
|-------|-------------|
| `/` | Control panel dashboard |
| `/playground` | Interactive API playground |
| `/api/*` | Proxied to HTTP server (requires API token) |
| `/execute/*` | Direct API execution (requires API token) |
| `/video/*` | Live video streaming endpoints |
| `/ws` | WebSocket connection for real-time communication |
| `/health` | Health check (no auth required) |

## Building

```bash
# From project root
docker build --platform linux/amd64 \
  --build-arg OWL_NONCE_HMAC_SECRET="your-hmac-secret" \
  --build-arg OWL_VM_PROFILE_DB_PASS="your-db-password" \
  -t owl-browser:amd64 \
  -f docker/Dockerfile .

# Or read from .env file
docker build --platform linux/amd64 \
  --build-arg OWL_NONCE_HMAC_SECRET=$(grep OWL_NONCE_HMAC_SECRET .env | cut -d'"' -f2) \
  --build-arg OWL_VM_PROFILE_DB_PASS=$(grep OWL_VM_PROFILE_DB_PASS .env | cut -d'"' -f2) \
  -t owl-browser:amd64 \
  -f docker/Dockerfile .
```

| Build Arg | Description |
|-----------|-------------|
| `OWL_NONCE_HMAC_SECRET` | HMAC secret for license validation |
| `OWL_VM_PROFILE_DB_PASS` | Password for encrypted VM profiles DB |

### What Gets Built

1. Downloads CEF (Chromium Embedded Framework)
2. Patches ANGLE with GPU spoofing wrapper
3. Downloads llama.cpp for AI features
4. Builds encrypted VM profiles database
5. Compiles headless browser and HTTP server
6. Builds React control panel

## Environment Variables

### Required

| Variable | Description |
|----------|-------------|
| `OWL_HTTP_TOKEN` | API authentication token (required unless using JWT) |

### Server Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_HTTP_HOST` | `0.0.0.0` | Server bind address |
| `OWL_HTTP_PORT` | `8080` | API server port |
| `OWL_HTTP_MAX_CONNECTIONS` | `100` | Maximum concurrent connections |
| `OWL_HTTP_TIMEOUT` | `30000` | Request timeout (ms) |
| `OWL_BROWSER_TIMEOUT` | `60000` | Browser command timeout (ms) |
| `OWL_BROWSER_PATH` | `/app/owl_browser` | Path to browser binary |

### Authentication

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_AUTH_MODE` | `token` | Auth mode: `token` or `jwt` |
| `OWL_JWT_PUBLIC_KEY` | - | Path to RSA public key (for JWT mode) |
| `OWL_JWT_ALGORITHM` | `RS256` | JWT signing algorithm |
| `OWL_DEV_MODE` | `false` | Disable auth for development |

### Control Panel

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_PANEL_PASSWORD` | `changeme` | Password to access web control panel |
| `OWL_PANEL_PORT` | `80` | Port for control panel (docker-compose) |

### Rate Limiting

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_RATE_LIMIT_ENABLED` | `false` | Enable rate limiting |
| `OWL_RATE_LIMIT_REQUESTS` | `100` | Requests per window |
| `OWL_RATE_LIMIT_WINDOW` | `60` | Window in seconds |
| `OWL_RATE_LIMIT_BURST` | `20` | Burst allowance |

### CORS

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_CORS_ENABLED` | `true` | Enable CORS |
| `OWL_CORS_ORIGINS` | `*` | Allowed origins |
| `OWL_CORS_METHODS` | `GET,POST,PUT,DELETE,OPTIONS` | Allowed methods |
| `OWL_CORS_HEADERS` | `Content-Type,Authorization` | Allowed headers |

### Logging

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_HTTP_VERBOSE` | `false` | Enable verbose logging |
| `OWL_LOG_REQUESTS` | `false` | Log all HTTP requests |

### IPC Tests

| Variable | Default | Description |
|----------|---------|-------------|
| `OWL_IPC_TESTS_ENABLED` | `true` | Enable IPC tests feature |
| `OWL_IPC_TEST_CLIENT_PATH` | `/app/ipc_test_client` | Path to ipc_test_client binary |
| `OWL_IPC_TEST_REPORTS_DIR` | `/app/reports` | Directory for test reports |

### License

| Variable | Description |
|----------|-------------|
| `OWL_LICENSE_CONTENT` | Base64-encoded license file content |
| `OWL_LICENSE_FILE` | Path to mounted license file |

### DNS

| Variable | Description |
|----------|-------------|
| `OWL_SKIP_DNS_CONFIG` | Set to skip default DNS configuration |

## Ports

| Port | Description |
|------|-------------|
| 80 | Web control panel + API gateway (Nginx reverse proxy) |
| 8080 | Direct REST API access (bypasses Nginx) |

**Note:** Port 80 (Nginx) proxies API requests to port 8080. You can use either:
- `http://localhost/api/...` - Via Nginx (recommended)
- `http://localhost:8080/...` - Direct to HTTP server

## Resources

- **Minimum**: 2 CPU, 2GB RAM, 5GB disk
- **Recommended**: 4 CPU, 4GB RAM, 10GB disk
- **Required**: `--shm-size=2g` for Chrome/CEF

## Export/Import Image

```bash
# Export
docker save owl-browser:amd64 | gzip > owl-browser-amd64.tar.gz

# Import
docker load < owl-browser-amd64.tar.gz
```

## IPC Tests Feature

The Docker image includes an end-to-end IPC test client that validates the browser's IPC protocol communication. This is useful for verifying browser functionality and diagnosing communication issues.

### Test Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| `smoke` | Quick validation (5 core operations) | Smoke testing, CI quick checks |
| `full` | Comprehensive (138+ methods) | Full regression testing |
| `benchmark` | Performance testing with iterations | Performance measurement |
| `stress` | Load testing with multiple contexts | Scalability testing |
| `leak-check` | Memory leak detection | Memory profiling |
| `parallel` | Concurrent context testing | Concurrency validation |

### Using the Web UI

1. Open the control panel at `http://localhost`
2. Click on the **Tests** tab in the sidebar
3. Select a test mode and configure options
4. Click **Run Test** to start
5. View progress and results in real-time
6. Access detailed HTML reports with charts and statistics

### API Endpoints

```bash
# Run a test
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"mode": "smoke"}' \
  http://localhost:8080/execute/ipc_tests_run

# Get test status
curl -X POST -H "Authorization: Bearer your-token" \
  http://localhost:8080/execute/ipc_tests_status

# List all reports
curl -X POST -H "Authorization: Bearer your-token" \
  http://localhost:8080/execute/ipc_tests_list_reports

# Get JSON report
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"run_id": "run_20241217_123456", "format": "json"}' \
  http://localhost:8080/execute/ipc_tests_get_report

# Get HTML report
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"run_id": "run_20241217_123456", "format": "html"}' \
  http://localhost:8080/execute/ipc_tests_get_report

# Abort running test
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"run_id": "run_20241217_123456"}' \
  http://localhost:8080/execute/ipc_tests_abort

# Delete report
curl -X POST -H "Authorization: Bearer your-token" \
  -H "Content-Type: application/json" \
  -d '{"run_id": "run_20241217_123456"}' \
  http://localhost:8080/execute/ipc_tests_delete_report
```

### Test Configuration Options

| Parameter | Mode | Default | Description |
|-----------|------|---------|-------------|
| `mode` | All | `full` | Test mode to run |
| `verbose` | All | `false` | Enable verbose output |
| `iterations` | benchmark | `1` | Number of benchmark iterations |
| `contexts` | stress | `10` | Number of browser contexts |
| `duration` | stress, leak-check | `60` | Duration in seconds |
| `concurrency` | parallel | `1` | Number of concurrent threads |

### Report Contents

Test reports include:
- **Summary**: Total, passed, failed, skipped test counts
- **Latency Statistics**: Min, max, avg, median, P95, P99, stddev
- **Resource Usage**: Memory and CPU timeline data
- **Per-Category Results**: Breakdown by test category
- **Failure Details**: Detailed information for failed tests
- **Interactive Charts**: Visual representations (HTML reports only)
