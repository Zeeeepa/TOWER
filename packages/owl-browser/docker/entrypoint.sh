#!/bin/bash
# =============================================================================
# Owl Browser Docker Entrypoint
# =============================================================================
# Configures and starts the HTTP server with the embedded browser.
# =============================================================================

set -e

# -----------------------------------------------------------------------------
# DNS Configuration
# -----------------------------------------------------------------------------

# Set default DNS servers (Cloudflare and Google) if not overridden by Docker
if [ -z "$OWL_SKIP_DNS_CONFIG" ]; then
    echo "nameserver 1.1.1.1" > /etc/resolv.conf
    echo "nameserver 8.8.8.8" >> /etc/resolv.conf
fi

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

# Browser path is always in /app for Docker
export OWL_BROWSER_PATH="${OWL_BROWSER_PATH:-/app/owl_browser}"

# License directory
LICENSE_DIR="/root/.config/owl-browser"
LICENSE_FILE="$LICENSE_DIR/license.olic"

# -----------------------------------------------------------------------------
# License Installation
# -----------------------------------------------------------------------------

# Create license directory
mkdir -p "$LICENSE_DIR"

# Install license from base64 environment variable if provided
if [ -n "$OWL_LICENSE_CONTENT" ]; then
    echo "Installing license from OWL_LICENSE_CONTENT..."
    echo "$OWL_LICENSE_CONTENT" | base64 -d > "$LICENSE_FILE"
    if [ -f "$LICENSE_FILE" ]; then
        echo "License installed to $LICENSE_FILE"
    else
        echo "ERROR: Failed to install license"
    fi
fi

# Install license from file path if provided
if [ -n "$OWL_LICENSE_FILE" ] && [ -f "$OWL_LICENSE_FILE" ]; then
    echo "Installing license from $OWL_LICENSE_FILE..."
    cp "$OWL_LICENSE_FILE" "$LICENSE_FILE"
    echo "License installed to $LICENSE_FILE"
fi

# Check license status (don't run browser here - it interferes with HTTP server startup)
if [ -f "$LICENSE_FILE" ]; then
    echo "License file found at $LICENSE_FILE"
else
    echo "WARNING: No license file found. Server will start in LIMITED MODE."
    echo "  Set OWL_LICENSE_CONTENT (base64) or OWL_LICENSE_FILE to install license."
fi
echo ""

# Verify browser binary exists
if [ ! -x "$OWL_BROWSER_PATH" ]; then
    echo "ERROR: Browser binary not found at $OWL_BROWSER_PATH"
    exit 1
fi

# Verify http server exists
if [ ! -x "/app/owl_http_server" ]; then
    echo "ERROR: HTTP server binary not found at /app/owl_http_server"
    exit 1
fi

# Check for required token (unless in development mode)
if [ -z "$OWL_HTTP_TOKEN" ] && [ -z "$OWL_JWT_PUBLIC_KEY" ] && [ "$OWL_DEV_MODE" != "true" ]; then
    echo "WARNING: No authentication configured!"
    echo "  Set OWL_HTTP_TOKEN for bearer token authentication"
    echo "  Or set OWL_JWT_PUBLIC_KEY for JWT authentication"
    echo "  Or set OWL_DEV_MODE=true to disable authentication (not recommended)"
    echo ""
fi

# -----------------------------------------------------------------------------
# Display Configuration
# -----------------------------------------------------------------------------

echo "=============================================="
echo "  Owl Browser HTTP Server"
echo "=============================================="
echo ""
echo "Configuration:"
echo "  Host:           ${OWL_HTTP_HOST:-0.0.0.0}"
echo "  Port:           ${OWL_HTTP_PORT:-8080}"
echo "  Browser:        $OWL_BROWSER_PATH"
echo "  Max Connections: ${OWL_HTTP_MAX_CONNECTIONS:-100}"
echo "  Request Timeout: ${OWL_HTTP_TIMEOUT:-30000}ms"
echo "  Browser Timeout: ${OWL_BROWSER_TIMEOUT:-60000}ms"
echo ""
echo "Security:"
if [ -n "$OWL_HTTP_TOKEN" ]; then
    echo "  Auth Mode:      Bearer Token"
elif [ -n "$OWL_JWT_PUBLIC_KEY" ]; then
    echo "  Auth Mode:      JWT (${OWL_JWT_ALGORITHM:-RS256})"
else
    echo "  Auth Mode:      None (WARNING: Not recommended for production)"
fi
echo "  Rate Limiting:  ${OWL_RATE_LIMIT_ENABLED:-false}"
echo "  IP Whitelist:   ${OWL_IP_WHITELIST_ENABLED:-false}"
echo "  SSL/TLS:        ${OWL_SSL_ENABLED:-false}"
echo ""
echo "Features:"
echo "  CORS Enabled:   ${OWL_CORS_ENABLED:-true}"
echo "  Verbose Logs:   ${OWL_HTTP_VERBOSE:-false}"
echo "  Request Logs:   ${OWL_LOG_REQUESTS:-false}"
echo ""
echo "Control Panel:"
echo "  URL:            http://localhost (via Nginx)"
echo "  Password:       [Set via OWL_PANEL_PASSWORD]"
echo ""
echo "=============================================="
echo ""

# -----------------------------------------------------------------------------
# Start Nginx
# -----------------------------------------------------------------------------

# Check if Nginx is available
if [ -x /usr/sbin/nginx ]; then
    echo "Starting Nginx (web control panel)..."

    # Verify nginx config
    nginx -t 2>/dev/null || {
        echo "WARNING: Nginx configuration test failed, skipping Nginx startup"
    }

    # Start nginx in the background
    nginx &
    NGINX_PID=$!
    echo "Nginx started (PID: $NGINX_PID)"
    echo ""

    # Trap to stop nginx on exit
    trap "echo 'Stopping Nginx...'; kill $NGINX_PID 2>/dev/null" EXIT
else
    echo "Nginx not installed, control panel not available"
    echo ""
fi

# -----------------------------------------------------------------------------
# Start Server
# -----------------------------------------------------------------------------

echo "Starting HTTP server..."
exec /app/owl_http_server "$@"
