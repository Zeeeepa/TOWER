# =============================================================================
# Owl Browser Docker Image
# =============================================================================
# Multi-stage build for Ubuntu amd64 (x86_64)
#
# This Dockerfile builds the headless browser, HTTP server, and web control panel
# creating a standalone container that provides REST API and web UI access.
# Note: LlamaServer (AI features) is disabled for a lighter image.
#
# Build: docker build -t olib-browser:latest -f docker/Dockerfile .
# Run:   docker run -p 80:80 -e OWL_HTTP_TOKEN=your-token -e OWL_PANEL_PASSWORD=your-password olib-browser:latest
# =============================================================================

# -----------------------------------------------------------------------------
# Stage 1: Build Environment
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build essentials and dependencies
RUN apt-get update && apt-get install -y \
    # Build essentials
    build-essential \
    cmake \
    git \
    pkg-config \
    python3 \
    # Node.js (for download scripts)
    curl \
    # CEF and browser dependencies
    libmaxminddb-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev \
    # SQLCipher (encrypted SQLite for VM profiles)
    libsqlcipher-dev \
    sqlcipher \
    # UUID library (for ipc_test_client)
    uuid-dev \
    # GTK3 and UI dependencies (needed for CEF even in headless mode)
    libgtk-3-dev \
    libcairo2-dev \
    librsvg2-dev \
    libx11-dev \
    libxi-dev \
    libxrandr-dev \
    # CEF runtime dependencies
    libglib2.0-dev \
    libnspr4-dev \
    libnss3-dev \
    libatk1.0-dev \
    libatk-bridge2.0-dev \
    libcups2-dev \
    libdrm-dev \
    libxkbcommon-dev \
    libxcomposite-dev \
    libxdamage-dev \
    libgbm-dev \
    libpango1.0-dev \
    libasound2-dev \
    # Additional utilities
    wget \
    unzip \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Node.js 20.x LTS
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Copy package files first for better caching
COPY package.json package-lock.json* ./

# Install npm dependencies (scripts only, no native builds yet)
RUN npm install --ignore-scripts

# Copy download scripts
COPY scripts/ ./scripts/

# Download CEF binary for Linux x86_64
# Note: We skip ANGLE patching on Linux - use SwiftShader for software rendering
# GPU fingerprint spoofing is handled by blocking WEBGL_debug_renderer_info extension
RUN node scripts/download-cef.cjs

# Copy the rest of the source code
COPY . .

# License server configuration (must be passed via --build-arg)
# Example: docker build --build-arg OWL_NONCE_HMAC_SECRET=your_key ...
ARG OWL_LICENSE_SERVER_URL=https://license.owlbrowser.net
ARG OWL_NONCE_HMAC_SECRET

# VM profile database password (for encrypted SQLite)
ARG OWL_VM_PROFILE_DB_PASS

# Verify build args are set (fail early if missing)
RUN if [ -z "${OWL_VM_PROFILE_DB_PASS}" ]; then \
      echo "ERROR: OWL_VM_PROFILE_DB_PASS build arg is required" && exit 1; \
    fi && \
    echo "VM profile DB password length: ${#OWL_VM_PROFILE_DB_PASS} chars"

# Build the encrypted VM profiles database (must be done before CMake)
# CRITICAL: Remove any existing key header to ensure fresh generation with correct password
RUN rm -f include/stealth/owl_vm_db_key.h data/profiles/vm_profiles.db \
    && OWL_VM_PROFILE_DB_PASS="${OWL_VM_PROFILE_DB_PASS}" npm run build:profiles \
    && echo "Generated key header:" && head -20 include/stealth/owl_vm_db_key.h

# Build the headless browser (without LlamaServer for lighter image)
RUN mkdir -p build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_HEADLESS=ON \
        -DBUILD_UI=OFF \
        -DBUILD_WITH_LLAMA=OFF \
        -DOWL_LICENSE_SERVER_URL="${OWL_LICENSE_SERVER_URL}" \
        -DOWL_NONCE_HMAC_SECRET="${OWL_NONCE_HMAC_SECRET}" \
    && make owl_browser -j$(nproc)

# Build the HTTP server
RUN cd build && make owl_http_server -j$(nproc)

# Build the IPC test client (end-to-end IPC tests)
# Clean any cached CMake files from local builds to avoid conflicts
RUN rm -rf e2e-ipc-tests/build && mkdir -p e2e-ipc-tests/build && cd e2e-ipc-tests/build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

# -----------------------------------------------------------------------------
# Stage 2: Frontend Builder
# -----------------------------------------------------------------------------
FROM node:20-alpine AS frontend-builder

WORKDIR /frontend

# Copy package files for caching
COPY docker/frontend/package.json docker/frontend/package-lock.json* ./

# Install dependencies
RUN npm ci --legacy-peer-deps 2>/dev/null || npm install

# Copy frontend source
COPY docker/frontend/ ./

# Build the frontend
RUN npm run build

# -----------------------------------------------------------------------------
# Stage 3: Runtime Image
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies only (no dev packages)
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Core runtime libs
    libcurl4 \
    libmaxminddb0 \
    libssl3 \
    zlib1g \
    # SQLCipher runtime (for encrypted VM profiles database)
    libsqlcipher0 \
    # GTK3 runtime (needed for CEF)
    libgtk-3-0 \
    libcairo2 \
    librsvg2-2 \
    libx11-6 \
    libxi6 \
    libxrandr2 \
    # CEF runtime dependencies
    libglib2.0-0 \
    libnspr4 \
    libnss3 \
    libatk1.0-0 \
    libatk-bridge2.0-0 \
    libcups2 \
    libdrm2 \
    libxkbcommon0 \
    libxcomposite1 \
    libxdamage1 \
    libgbm1 \
    libpango-1.0-0 \
    libasound2 \
    libxss1 \
    # Font support
    fonts-liberation \
    fonts-noto-color-emoji \
    # Web server for control panel
    nginx \
    # Utilities
    ca-certificates \
    dumb-init \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy browser binary and resources from builder
COPY --from=builder /build/build/owl_browser /app/owl_browser
COPY --from=builder /build/build/owl_http_server /app/owl_http_server
COPY --from=builder /build/e2e-ipc-tests/build/ipc_test_client /app/ipc_test_client

# Copy shared libraries from build directory
COPY --from=builder /build/build/*.so* /app/

# Copy CEF resource files (required for browser initialization)
# Resources directory: pak files, ICU data, locales
COPY --from=builder /build/third_party/cef_linux/Resources/*.pak /app/
COPY --from=builder /build/third_party/cef_linux/Resources/*.dat /app/
COPY --from=builder /build/third_party/cef_linux/Resources/locales /app/locales

# Copy custom Owl resources (logo, etc.) for homepage
COPY --from=builder /build/resources/Olib-owl.apng /app/
COPY --from=builder /build/resources/Olib-owl.mp4 /app/

# Release directory: V8 snapshot and other binaries
COPY --from=builder /build/third_party/cef_linux/Release/*.bin /app/
COPY --from=builder /build/third_party/cef_linux/Release/*.json /app/

# CEF shared libraries including patched ANGLE wrapper (CRITICAL for GPU spoofing)
# libGLESv2.so is the patched wrapper, libGLESv2_original.so is the original
COPY --from=builder /build/third_party/cef_linux/Release/*.so /app/
COPY --from=builder /build/third_party/cef_linux/Release/*.so.* /app/

# Copy GeoIP database
COPY --from=builder /build/build/third_party /app/third_party

# Copy statics (test pages)
COPY --from=builder /build/build/statics /app/statics

# Copy VM profiles database (encrypted SQLite)
COPY --from=builder /build/data/profiles/vm_profiles.db /app/data/profiles/vm_profiles.db

# Copy entrypoint script
COPY docker/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

# Copy frontend build from frontend-builder stage
COPY --from=frontend-builder /frontend/dist /app/static

# Copy Nginx configuration
COPY docker/nginx/nginx.conf /etc/nginx/nginx.conf

# Remove default nginx site
RUN rm -f /etc/nginx/sites-enabled/default

# Set library path for shared libraries
ENV LD_LIBRARY_PATH=/app

# Create reports directory for IPC tests
RUN mkdir -p /app/reports

# Environment variables with defaults
ENV OWL_BROWSER_PATH=/app/owl_browser
ENV OWL_IPC_TESTS_ENABLED=true
ENV OWL_IPC_TEST_CLIENT_PATH=/app/ipc_test_client
ENV OWL_IPC_TEST_REPORTS_DIR=/app/reports
ENV OWL_HTTP_HOST=0.0.0.0
ENV OWL_HTTP_PORT=8080
ENV OWL_HTTP_MAX_CONNECTIONS=100
ENV OWL_HTTP_TIMEOUT=30000
ENV OWL_BROWSER_TIMEOUT=60000
ENV OWL_HTTP_VERBOSE=false
ENV OWL_LOG_REQUESTS=false

# Rate limiting (disabled by default)
ENV OWL_RATE_LIMIT_ENABLED=false
ENV OWL_RATE_LIMIT_REQUESTS=100
ENV OWL_RATE_LIMIT_WINDOW=60
ENV OWL_RATE_LIMIT_BURST=20

# CORS (enabled by default)
ENV OWL_CORS_ENABLED=true
ENV OWL_CORS_ORIGINS=*
ENV OWL_CORS_METHODS=GET,POST,PUT,DELETE,OPTIONS
ENV OWL_CORS_HEADERS=Content-Type,Authorization

# Panel password (for web control panel login)
ENV OWL_PANEL_PASSWORD=changeme

# Expose ports (80 for Nginx/frontend, 8080 for internal API)
EXPOSE 80 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:${OWL_HTTP_PORT}/health || exit 1

# Use dumb-init to handle signals properly
ENTRYPOINT ["/usr/bin/dumb-init", "--"]

# Run entrypoint script
CMD ["/app/entrypoint.sh"]
