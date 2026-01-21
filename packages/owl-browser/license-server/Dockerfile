# ==============================================================================
# Owl Browser License Server - Monolithic Docker Image
# ==============================================================================
# This image contains:
# - PostgreSQL 17 database
# - Admin Server (port 3035)
# - Subscription/License API Server (port 3034)
# - Customer Portal (integrated with Admin Server at /portal)
# - License Generator (compiled for Linux)
#
# Key Management:
# - RSA key pair should be mounted to /app/keys/
# - Keys are used for: signing API responses, generating license files
# - The SAME keys must be used to build the browser (embedded public key)
#
# Usage:
#   docker build -t owl-license-server .
#   docker run -d -p 3034:3034 -p 3035:3035 \
#     -v ./keys:/app/keys \
#     -v ./data:/app/data \
#     owl-license-server
# ==============================================================================

FROM ubuntu:24.04

LABEL maintainer="Olib AI"
LABEL description="Owl Browser License Server - Monolithic deployment with PostgreSQL 17"
LABEL version="1.0"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# ==============================================================================
# Install system dependencies and PostgreSQL 17
# ==============================================================================

RUN apt-get update && apt-get install -y \
    # Basic utilities
    curl \
    wget \
    gnupg2 \
    lsb-release \
    ca-certificates \
    # Python
    python3 \
    python3-pip \
    python3-venv \
    # Build tools (for psycopg2 and license_generator)
    gcc \
    g++ \
    make \
    python3-dev \
    libpq-dev \
    # OpenSSL development libraries (for license_generator)
    libssl-dev \
    # Process management
    supervisor \
    # Networking tools
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Add PostgreSQL 17 repository
RUN sh -c 'echo "deb http://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list' \
    && wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -

# Install PostgreSQL 17
RUN apt-get update && apt-get install -y \
    postgresql-17 \
    postgresql-client-17 \
    && rm -rf /var/lib/apt/lists/*

# ==============================================================================
# Create application user and directories
# ==============================================================================

RUN useradd -m -s /bin/bash olib \
    && mkdir -p /app/data /app/licenses /app/keys /app/logs \
    && chown -R olib:olib /app

# ==============================================================================
# Set up Python virtual environment
# ==============================================================================

WORKDIR /app

# Create virtual environment
RUN python3 -m venv /app/venv
ENV PATH="/app/venv/bin:$PATH"
ENV VIRTUAL_ENV="/app/venv"

# Upgrade pip
RUN pip install --upgrade pip setuptools wheel

# ==============================================================================
# Install Python dependencies
# ==============================================================================

# Copy requirements first for better caching
COPY requirements.txt /app/
RUN pip install --no-cache-dir -r requirements.txt

# Install PostgreSQL driver (psycopg2)
RUN pip install --no-cache-dir psycopg2-binary>=2.9.0

# ==============================================================================
# Copy application code
# ==============================================================================

COPY src/*.py /app/
COPY templates/ /app/templates/
COPY static/ /app/static/

# ==============================================================================
# Build License Generator for Linux
# ==============================================================================
# NOTE: license_generator.cpp must be copied to docker/license_generator.cpp
# before building. This is handled by docker-compose or the build script.
# ==============================================================================

# Copy the license generator source (from docker/ subdirectory)
COPY docker/license_generator.cpp /app/build/license_generator.cpp

# Compile for Linux using OpenSSL
RUN cd /app/build && \
    g++ -std=c++17 -O2 -o /app/license_generator license_generator.cpp \
    -lssl -lcrypto && \
    rm -rf /app/build && \
    chmod +x /app/license_generator

# Create symlink for the keys directory that the generator expects
# This allows the license_generator to find keys at ~/.owl_license/
RUN mkdir -p /root/.owl_license && \
    ln -sf /app/keys/private_key.pem /root/.owl_license/owl_license.key && \
    ln -sf /app/keys/public_key.pem /root/.owl_license/owl_license.pub

# ==============================================================================
# Configure PostgreSQL
# ==============================================================================

# Create PostgreSQL data directory (remove existing if any and reinitialize)
RUN rm -rf /var/lib/postgresql/17/main \
    && mkdir -p /var/lib/postgresql/17/main \
    && chown -R postgres:postgres /var/lib/postgresql

# Initialize PostgreSQL database cluster
USER postgres
RUN /usr/lib/postgresql/17/bin/initdb -D /var/lib/postgresql/17/main --encoding=UTF8 --locale=C

# Configure PostgreSQL
RUN echo "listen_addresses = 'localhost'" >> /var/lib/postgresql/17/main/postgresql.conf \
    && echo "port = 5432" >> /var/lib/postgresql/17/main/postgresql.conf \
    && echo "max_connections = 100" >> /var/lib/postgresql/17/main/postgresql.conf \
    && echo "shared_buffers = 128MB" >> /var/lib/postgresql/17/main/postgresql.conf \
    && echo "log_destination = 'stderr'" >> /var/lib/postgresql/17/main/postgresql.conf \
    && echo "logging_collector = off" >> /var/lib/postgresql/17/main/postgresql.conf

# Configure authentication
RUN echo "local all all trust" > /var/lib/postgresql/17/main/pg_hba.conf \
    && echo "host all all 127.0.0.1/32 md5" >> /var/lib/postgresql/17/main/pg_hba.conf \
    && echo "host all all ::1/128 md5" >> /var/lib/postgresql/17/main/pg_hba.conf

USER root

# ==============================================================================
# Configure Supervisor
# ==============================================================================

COPY docker/supervisord.conf /etc/supervisor/conf.d/supervisord.conf

# ==============================================================================
# Copy entrypoint script
# ==============================================================================

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# ==============================================================================
# Set environment variables
# ==============================================================================

# Database configuration (PostgreSQL)
ENV OLIB_DB_TYPE=postgresql
ENV OLIB_DB_HOST=localhost
ENV OLIB_DB_PORT=5432
ENV OLIB_DB_NAME=olib_licenses
ENV OLIB_DB_USER=olib
ENV OLIB_DB_PASSWORD=olib_secure_password

# Server configuration
ENV OLIB_LICENSE_SERVER_HOST=0.0.0.0
ENV OLIB_LICENSE_SERVER_PORT=3034
ENV OLIB_ADMIN_HOST=0.0.0.0
ENV OLIB_ADMIN_PORT=3035

# Session configuration
ENV SESSION_LIFETIME_HOURS=168
ENV NONCE_TIMESTAMP_TOLERANCE=300

# Paths
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1

# ==============================================================================
# Expose ports
# ==============================================================================

# Admin Server (with Customer Portal at /portal)
EXPOSE 3035

# Subscription/License API Server
EXPOSE 3034

# ==============================================================================
# Health check
# ==============================================================================

HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f http://localhost:3034/api/v1/health || exit 1

# ==============================================================================
# Volumes for persistent data
# ==============================================================================

VOLUME ["/app/data", "/app/licenses", "/app/keys", "/app/logs"]

# ==============================================================================
# Entrypoint
# ==============================================================================

ENTRYPOINT ["/entrypoint.sh"]
CMD ["supervisord", "-n", "-c", "/etc/supervisor/conf.d/supervisord.conf"]
