#!/bin/bash
# ==============================================================================
# Owl Browser License Server - Docker Entrypoint
# ==============================================================================
# This script initializes the database, sets up keys, and starts services.
# ==============================================================================

set -e

echo "=============================================="
echo "  Owl Browser License Server - Starting Up"
echo "=============================================="
echo ""

# ==============================================================================
# Create necessary directories
# ==============================================================================
echo "[1/6] Creating directories..."
mkdir -p /app/data /app/licenses /app/keys /app/logs
chown -R olib:olib /app/data /app/licenses /app/logs 2>/dev/null || true

# ==============================================================================
# Check/Generate RSA Keys
# ==============================================================================
echo "[2/6] Checking RSA key pair..."

PRIVATE_KEY="/app/keys/private_key.pem"
PUBLIC_KEY="/app/keys/public_key.pem"

if [ ! -f "$PRIVATE_KEY" ] || [ ! -f "$PUBLIC_KEY" ]; then
    if [ "${GENERATE_KEYS:-false}" = "true" ]; then
        echo "  -> Generating new RSA-2048 key pair..."
        openssl genpkey -algorithm RSA -out "$PRIVATE_KEY" -pkeyopt rsa_keygen_bits:2048
        openssl rsa -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY"
        chmod 600 "$PRIVATE_KEY"
        chmod 644 "$PUBLIC_KEY"
        echo "  -> Keys generated successfully!"
        echo ""
        echo "  ======================= IMPORTANT =========================="
        echo "  New RSA keys have been generated."
        echo "  "
        echo "  To use these with your browser, you must:"
        echo "  1. Copy the public key to your development machine"
        echo "  2. Update the embedded key in src/owl_license.cc"
        echo "  3. Rebuild the browser"
        echo "  "
        echo "  Or mount your existing keys at /app/keys/"
        echo "  ============================================================"
        echo ""
    else
        echo "  -> WARNING: RSA keys not found at /app/keys/"
        echo "  -> License signing will not work without keys!"
        echo "  -> Mount your keys or set GENERATE_KEYS=true"
        echo ""
    fi
else
    echo "  -> RSA keys found!"
    # Verify keys are valid
    if openssl rsa -in "$PRIVATE_KEY" -check -noout 2>/dev/null; then
        echo "  -> Private key is valid"
    else
        echo "  -> WARNING: Private key validation failed!"
    fi
fi

# Update symlinks for license_generator
mkdir -p /root/.owl_license
ln -sf "$PRIVATE_KEY" /root/.owl_license/owl_license.key 2>/dev/null || true
ln -sf "$PUBLIC_KEY" /root/.owl_license/owl_license.pub 2>/dev/null || true

# ==============================================================================
# Start PostgreSQL
# ==============================================================================
echo "[3/6] Starting PostgreSQL..."

# Ensure PostgreSQL data directory has correct permissions
chown -R postgres:postgres /var/lib/postgresql

# Create postgres log directory
mkdir -p /var/log/postgresql
chown postgres:postgres /var/log/postgresql

# Start PostgreSQL in background temporarily to initialize database
su - postgres -c "/usr/lib/postgresql/17/bin/pg_ctl -D /var/lib/postgresql/17/main -l /var/log/postgresql/postgresql_init.log start"

# Wait for PostgreSQL to be ready
echo "  -> Waiting for PostgreSQL to be ready..."
for i in {1..30}; do
    if su - postgres -c "psql -c 'SELECT 1'" >/dev/null 2>&1; then
        echo "  -> PostgreSQL is ready!"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "  -> ERROR: PostgreSQL failed to start!"
        cat /var/log/postgresql/postgresql_init.log
        exit 1
    fi
    sleep 1
done

# ==============================================================================
# Initialize Database
# ==============================================================================
echo "[4/6] Initializing database..."

# Check if database exists
DB_EXISTS=$(su - postgres -c "psql -tAc \"SELECT 1 FROM pg_database WHERE datname='${OLIB_DB_NAME:-olib_licenses}'\"")

if [ "$DB_EXISTS" != "1" ]; then
    echo "  -> Creating database user and database..."

    # Create user if not exists
    su - postgres -c "psql -c \"CREATE USER ${OLIB_DB_USER:-olib} WITH PASSWORD '${OLIB_DB_PASSWORD:-olib_secure_password}';\"" 2>/dev/null || true

    # Create database
    su - postgres -c "psql -c \"CREATE DATABASE ${OLIB_DB_NAME:-olib_licenses} OWNER ${OLIB_DB_USER:-olib};\"" 2>/dev/null || true

    # Grant privileges
    su - postgres -c "psql -c \"GRANT ALL PRIVILEGES ON DATABASE ${OLIB_DB_NAME:-olib_licenses} TO ${OLIB_DB_USER:-olib};\"" 2>/dev/null || true

    echo "  -> Database created!"
else
    echo "  -> Database already exists"
fi

# Run schema initialization
echo "  -> Running schema initialization..."
cd /app
/app/venv/bin/python init_database.py

echo "  -> Database initialized!"

# ==============================================================================
# Stop PostgreSQL (will be managed by supervisor)
# ==============================================================================
echo "[5/6] Preparing for supervisor..."
su - postgres -c "/usr/lib/postgresql/17/bin/pg_ctl -D /var/lib/postgresql/17/main stop" 2>/dev/null || true
sleep 2

# ==============================================================================
# Start Services via Supervisor
# ==============================================================================
echo "[6/6] Starting services..."
echo ""
echo "=============================================="
echo "  Services:"
echo "  - Admin Server:        http://0.0.0.0:${OLIB_ADMIN_PORT:-3035}"
echo "  - Customer Portal:     http://0.0.0.0:${OLIB_ADMIN_PORT:-3035}/portal"
echo "  - License API Server:  http://0.0.0.0:${OLIB_LICENSE_SERVER_PORT:-3034}"
echo "=============================================="
echo ""

# Execute the CMD (supervisord)
exec "$@"
