#!/bin/bash
# =============================================================================
# EC2 User Data Script for Owl Browser
# =============================================================================
# This script runs on first boot of an EC2 instance to install Docker
# and prepare the environment for running Owl Browser.
# =============================================================================

set -e
exec > >(tee /var/log/owl-browser-setup.log) 2>&1

echo "=========================================="
echo "  Owl Browser EC2 Setup"
echo "  $(date)"
echo "=========================================="

# Update system
echo "[1/6] Updating system packages..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get upgrade -y

# Install Docker
echo "[2/6] Installing Docker..."
apt-get install -y docker.io docker-compose
systemctl enable docker
systemctl start docker

# Install AWS CLI v2
echo "[3/6] Installing AWS CLI..."
apt-get install -y unzip curl
curl -s "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "/tmp/awscliv2.zip"
unzip -q /tmp/awscliv2.zip -d /tmp
/tmp/aws/install
rm -rf /tmp/aws /tmp/awscliv2.zip

# Add ubuntu user to docker group
echo "[4/6] Configuring permissions..."
usermod -aG docker ubuntu

# Create app directory
echo "[5/6] Creating application directory..."
mkdir -p /opt/owl-browser
cd /opt/owl-browser

# Create docker-compose.yml
cat > docker-compose.yml << 'COMPOSE_EOF'
version: '3.8'

services:
  owl-browser:
    image: ${ECR_IMAGE:-olib-browser:latest}
    container_name: owl-browser
    restart: unless-stopped
    ports:
      - "80:80"
      - "8080:8080"
    environment:
      - OWL_HTTP_TOKEN=${OWL_HTTP_TOKEN:-changeme}
      - OWL_PANEL_PASSWORD=${OWL_PANEL_PASSWORD:-changeme}
      - OWL_HTTP_HOST=0.0.0.0
      - OWL_HTTP_PORT=8080
      - OWL_HTTP_MAX_CONNECTIONS=${OWL_HTTP_MAX_CONNECTIONS:-100}
      - OWL_BROWSER_TIMEOUT=${OWL_BROWSER_TIMEOUT:-60000}
      - OWL_HTTP_TIMEOUT=${OWL_HTTP_TIMEOUT:-30000}
      - OWL_RATE_LIMIT_ENABLED=${OWL_RATE_LIMIT_ENABLED:-false}
      - OWL_CORS_ENABLED=${OWL_CORS_ENABLED:-true}
    shm_size: '2gb'
    deploy:
      resources:
        limits:
          memory: ${MEMORY_LIMIT:-14G}
        reservations:
          memory: ${MEMORY_RESERVATION:-8G}
    logging:
      driver: "json-file"
      options:
        max-size: "100m"
        max-file: "3"
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 60s
COMPOSE_EOF

# Create environment file template
cat > .env.template << 'ENV_EOF'
# =============================================================================
# Owl Browser Environment Configuration
# =============================================================================
# Copy this file to .env and fill in your values:
#   cp .env.template .env
#   nano .env
# =============================================================================

# ECR Image URI (required)
# Format: ACCOUNT_ID.dkr.ecr.REGION.amazonaws.com/olib-browser:TAG
ECR_IMAGE=

# Authentication (required - generate with: openssl rand -hex 32)
OWL_HTTP_TOKEN=
OWL_PANEL_PASSWORD=

# Performance tuning (optional)
OWL_HTTP_MAX_CONNECTIONS=100
OWL_BROWSER_TIMEOUT=60000
OWL_HTTP_TIMEOUT=30000

# Memory limits (adjust based on instance size)
# c7i.xlarge (8GB): MEMORY_LIMIT=6G, MEMORY_RESERVATION=4G
# c7i.2xlarge (16GB): MEMORY_LIMIT=14G, MEMORY_RESERVATION=8G
# c7i.4xlarge (32GB): MEMORY_LIMIT=28G, MEMORY_RESERVATION=16G
MEMORY_LIMIT=14G
MEMORY_RESERVATION=8G

# Rate limiting (optional)
OWL_RATE_LIMIT_ENABLED=false

# CORS (optional)
OWL_CORS_ENABLED=true
ENV_EOF

# Create helper scripts
cat > start.sh << 'START_EOF'
#!/bin/bash
# Start Owl Browser
cd /opt/owl-browser
docker-compose up -d
docker-compose logs -f
START_EOF
chmod +x start.sh

cat > stop.sh << 'STOP_EOF'
#!/bin/bash
# Stop Owl Browser
cd /opt/owl-browser
docker-compose down
STOP_EOF
chmod +x stop.sh

cat > update.sh << 'UPDATE_EOF'
#!/bin/bash
# Update Owl Browser to latest image
set -e
cd /opt/owl-browser

# Source environment
source .env

# Extract region from ECR URI
ECR_REGION=$(echo $ECR_IMAGE | sed 's/.*\.ecr\.\([^.]*\)\..*/\1/')
ECR_HOST=$(echo $ECR_IMAGE | cut -d'/' -f1)

echo "Logging into ECR..."
aws ecr get-login-password --region $ECR_REGION | docker login --username AWS --password-stdin $ECR_HOST

echo "Pulling latest image..."
docker-compose pull

echo "Restarting with new image..."
docker-compose up -d

echo "Done! New container status:"
docker-compose ps
UPDATE_EOF
chmod +x update.sh

cat > logs.sh << 'LOGS_EOF'
#!/bin/bash
# View Owl Browser logs
cd /opt/owl-browser
docker-compose logs -f --tail=100
LOGS_EOF
chmod +x logs.sh

# Set ownership
chown -R ubuntu:ubuntu /opt/owl-browser

echo "[6/6] Setup complete!"
echo ""
echo "=========================================="
echo "  Next Steps:"
echo "=========================================="
echo "1. SSH into this instance"
echo "2. cd /opt/owl-browser"
echo "3. cp .env.template .env"
echo "4. nano .env  # Configure your settings"
echo "5. ./update.sh  # Pull image from ECR"
echo "6. ./start.sh  # Start the container"
echo ""
echo "Helper scripts:"
echo "  ./start.sh  - Start container"
echo "  ./stop.sh   - Stop container"
echo "  ./update.sh - Pull latest image and restart"
echo "  ./logs.sh   - View logs"
echo "=========================================="
