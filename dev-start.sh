#!/bin/bash
# ==============================================================================
# TOWER Web2API - Quick Start Development Script
# ==============================================================================
# This script starts the essential services for development and testing.
# 
# Usage:
#   ./dev-start.sh          # Start all services
#   ./dev-start.sh postgres # Start only PostgreSQL
#   ./dev-start.sh backend  # Start only backend (requires PostgreSQL)
# ==============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${GREEN}=============================================="
echo "  TOWER Web2API - Development Setup"
echo -e "==============================================${NC}"

# Function to start PostgreSQL
start_postgres() {
    echo -e "${YELLOW}Starting PostgreSQL...${NC}"
    docker run -d \
        --name tower-postgres \
        -e POSTGRES_USER=web2api \
        -e POSTGRES_PASSWORD=web2api123 \
        -e POSTGRES_DB=web2api \
        -p 5432:5432 \
        -v tower-postgres-data:/var/lib/postgresql/data \
        postgres:16-alpine \
        || echo "PostgreSQL may already be running"
    
    # Wait for PostgreSQL to be ready
    echo "Waiting for PostgreSQL to be ready..."
    sleep 5
    until docker exec tower-postgres pg_isready -U web2api 2>/dev/null; do
        echo "  Waiting..."
        sleep 2
    done
    echo -e "${GREEN}PostgreSQL is ready!${NC}"
}

# Function to install Python dependencies
install_python_deps() {
    echo -e "${YELLOW}Installing Python dependencies...${NC}"
    cd "$SCRIPT_DIR/backend"
    
    # Create virtual environment if it doesn't exist
    if [ ! -d ".venv" ]; then
        python3 -m venv .venv
    fi
    
    # Activate virtual environment
    source .venv/bin/activate
    
    # Install dependencies
    pip install -q --upgrade pip
    pip install -q -e ".[dev]" 2>/dev/null || pip install -q -e .
    
    echo -e "${GREEN}Python dependencies installed!${NC}"
}

# Function to start backend
start_backend() {
    echo -e "${YELLOW}Starting Backend...${NC}"
    cd "$SCRIPT_DIR/backend"
    
    # Load environment variables
    set -a
    source .env 2>/dev/null || true
    set +a
    
    # Export required variables
    export DATABASE_URL="${DATABASE_URL:-postgresql+asyncpg://web2api:web2api123@localhost:5432/web2api}"
    export API_HOST="${API_HOST:-0.0.0.0}"
    export API_PORT="${API_PORT:-8000}"
    export PYTHONPATH="$SCRIPT_DIR/backend/src:$PYTHONPATH"
    
    # Start the server
    echo -e "${GREEN}Starting API server on http://localhost:8000${NC}"
    echo -e "  Documentation: http://localhost:8000/docs"
    echo ""
    
    # Activate venv and run
    source .venv/bin/activate 2>/dev/null || true
    python -m uvicorn web2api.api.main:create_app --factory --host 0.0.0.0 --port 8000 --reload
}

# Function to stop all services
stop_all() {
    echo -e "${YELLOW}Stopping all services...${NC}"
    docker stop tower-postgres 2>/dev/null || true
    docker rm tower-postgres 2>/dev/null || true
    echo -e "${GREEN}Services stopped!${NC}"
}

# Main logic
case "${1:-all}" in
    postgres)
        start_postgres
        ;;
    backend)
        start_backend
        ;;
    install)
        install_python_deps
        ;;
    stop)
        stop_all
        ;;
    all)
        start_postgres
        install_python_deps
        start_backend
        ;;
    *)
        echo "Usage: $0 {all|postgres|backend|install|stop}"
        echo ""
        echo "Commands:"
        echo "  all      - Start PostgreSQL and backend (default)"
        echo "  postgres - Start only PostgreSQL container"
        echo "  backend  - Start only backend server"
        echo "  install  - Install Python dependencies"
        echo "  stop     - Stop all services"
        exit 1
        ;;
esac
