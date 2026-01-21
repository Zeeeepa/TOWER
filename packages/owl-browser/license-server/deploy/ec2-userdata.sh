#!/bin/bash
# ==============================================================================
# EC2 User Data Script - Initial Setup
# ==============================================================================
#
# This script can be used as EC2 User Data when launching an instance,
# or run manually on a fresh EC2 instance to set up the environment.
#
# Prerequisites:
#   - Amazon Linux 2023 or Ubuntu 22.04
#   - IAM Role with ECR pull permissions attached to the instance
#
# Usage as User Data:
#   Copy this entire script into the User Data field when launching EC2
#
# Manual usage:
#   sudo bash ec2-userdata.sh
#
# ==============================================================================

set -e

# Log file
exec > >(tee /var/log/owl-license-setup.log) 2>&1

echo "=============================================="
echo "  Owl License Server - EC2 Setup"
echo "  $(date)"
echo "=============================================="

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    OS="unknown"
fi

echo "Detected OS: $OS"

# ==============================================================================
# Install Docker
# ==============================================================================
echo "[1/5] Installing Docker..."

if [ "$OS" = "amzn" ]; then
    # Amazon Linux
    yum update -y
    yum install -y docker aws-cli
    systemctl start docker
    systemctl enable docker
    usermod -aG docker ec2-user
elif [ "$OS" = "ubuntu" ]; then
    # Ubuntu
    apt-get update
    apt-get install -y docker.io awscli
    systemctl start docker
    systemctl enable docker
    usermod -aG docker ubuntu
else
    echo "Unsupported OS: $OS"
    exit 1
fi

echo "Docker installed successfully"

# ==============================================================================
# Create application directory
# ==============================================================================
echo "[2/5] Creating application directory..."

APP_DIR="/opt/owl-license-server"
mkdir -p $APP_DIR/{keys,data,licenses,logs}

if [ "$OS" = "amzn" ]; then
    chown -R ec2-user:ec2-user $APP_DIR
elif [ "$OS" = "ubuntu" ]; then
    chown -R ubuntu:ubuntu $APP_DIR
fi

echo "Application directory created at $APP_DIR"

# ==============================================================================
# Create secrets.env template
# ==============================================================================
echo "[3/5] Creating configuration files..."

cat > $APP_DIR/secrets.env << 'EOF'
# ==============================================================================
# Owl License Server - Secrets Configuration
# ==============================================================================
# IMPORTANT: Update these values before starting the service!
#
# Generate password hash:
#   python3 -c "from werkzeug.security import generate_password_hash; print(generate_password_hash('your-password'))"
#
# Generate secret key:
#   python3 -c "import secrets; print(secrets.token_hex(32))"
# ==============================================================================

# Admin authentication
OLIB_ADMIN_PASSWORD_HASH=scrypt:32768:8:1$placeholder$change_this

# Admin secret key for sessions
OLIB_ADMIN_SECRET_KEY=change-this-to-a-secure-random-string

# Webhook API key (for payment provider integration)
OLIB_WEBHOOK_API_KEY=change-this-webhook-key

# HMAC secret for request authentication (MUST match browser build)
OWL_NONCE_HMAC_SECRET=change-this-hmac-secret
EOF

# ==============================================================================
# Create docker-compose.yml
# ==============================================================================
cat > $APP_DIR/docker-compose.yml << 'EOF'
version: '3.8'

services:
  owl-license-server:
    image: ${ECR_IMAGE:-owl-license-server:latest}
    container_name: owl-license-server
    restart: unless-stopped

    ports:
      - "3035:3035"  # Admin + Customer Portal
      - "3034:3034"  # License API

    volumes:
      - ./keys:/app/keys:ro
      - ./data:/app/data
      - ./licenses:/app/licenses
      - ./logs:/app/logs
      - owl-db-data:/var/lib/postgresql/17/main

    env_file:
      - secrets.env

    environment:
      - OLIB_DB_TYPE=postgresql
      - OLIB_DB_HOST=localhost
      - OLIB_DB_PORT=5432
      - OLIB_DB_NAME=olib_licenses
      - OLIB_DB_USER=olib
      - OLIB_DB_PASSWORD=${DB_PASSWORD:-olib_secure_password}
      - OLIB_ADMIN_HOST=0.0.0.0
      - OLIB_ADMIN_PORT=3035
      - OLIB_LICENSE_SERVER_HOST=0.0.0.0
      - OLIB_LICENSE_SERVER_PORT=3034

    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:3034/api/v1/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 60s

volumes:
  owl-db-data:
    driver: local
EOF

# ==============================================================================
# Create systemd service
# ==============================================================================
echo "[4/5] Creating systemd service..."

cat > /etc/systemd/system/owl-license-server.service << EOF
[Unit]
Description=Owl License Server
Requires=docker.service
After=docker.service

[Service]
Type=simple
WorkingDirectory=$APP_DIR
ExecStartPre=-/usr/bin/docker stop owl-license-server
ExecStartPre=-/usr/bin/docker rm owl-license-server
ExecStart=/usr/bin/docker compose up
ExecStop=/usr/bin/docker compose down
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable owl-license-server

# ==============================================================================
# Create update script
# ==============================================================================
echo "[5/5] Creating helper scripts..."

cat > $APP_DIR/update.sh << 'EOF'
#!/bin/bash
# Update Owl License Server to latest image

set -e

# Get AWS region from instance metadata
TOKEN=$(curl -X PUT "http://169.254.169.254/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 21600" 2>/dev/null)
REGION=$(curl -H "X-aws-ec2-metadata-token: $TOKEN" http://169.254.169.254/latest/meta-data/placement/region 2>/dev/null)
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)

ECR_REGISTRY="${ACCOUNT_ID}.dkr.ecr.${REGION}.amazonaws.com"
ECR_IMAGE="${ECR_REGISTRY}/owl-license-server:latest"

echo "Authenticating to ECR..."
aws ecr get-login-password --region $REGION | docker login --username AWS --password-stdin $ECR_REGISTRY

echo "Pulling latest image..."
docker pull $ECR_IMAGE

echo "Restarting service..."
systemctl restart owl-license-server

echo "Done! Checking health..."
sleep 10
curl -s http://localhost:3034/api/v1/health | jq .
EOF

chmod +x $APP_DIR/update.sh

# Create restart script
cat > $APP_DIR/restart.sh << 'EOF'
#!/bin/bash
# Restart Owl License Server
systemctl restart owl-license-server
sleep 5
docker ps | grep owl-license-server
EOF

chmod +x $APP_DIR/restart.sh

# Create logs script
cat > $APP_DIR/logs.sh << 'EOF'
#!/bin/bash
# View Owl License Server logs
docker logs -f owl-license-server
EOF

chmod +x $APP_DIR/logs.sh

echo ""
echo "=============================================="
echo "  Setup Complete!"
echo "=============================================="
echo ""
echo "Next steps:"
echo "  1. Copy your RSA keys to $APP_DIR/keys/"
echo "     - private_key.pem"
echo "     - public_key.pem"
echo ""
echo "  2. Edit secrets configuration:"
echo "     nano $APP_DIR/secrets.env"
echo ""
echo "  3. Pull the Docker image:"
echo "     cd $APP_DIR && ./update.sh"
echo ""
echo "  4. Start the service:"
echo "     systemctl start owl-license-server"
echo ""
echo "Helper scripts:"
echo "  $APP_DIR/update.sh   - Pull latest image and restart"
echo "  $APP_DIR/restart.sh  - Restart the service"
echo "  $APP_DIR/logs.sh     - View logs"
echo ""
