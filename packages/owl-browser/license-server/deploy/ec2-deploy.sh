#!/bin/bash
# ==============================================================================
# Deploy Owl License Server to AWS EC2
# ==============================================================================
#
# This script:
#   1. Connects to your EC2 instance via SSH
#   2. Pulls the latest image from ECR
#   3. Restarts the service with the new image
#
# Prerequisites:
#   - EC2 instance with Docker installed
#   - EC2 instance role with ECR pull permissions
#   - SSH key configured for EC2 access
#   - Security groups allowing ports 3034 and 3035
#
# Usage:
#   ./deploy/ec2-deploy.sh --host ec2-xx-xx-xx-xx.compute-1.amazonaws.com
#   ./deploy/ec2-deploy.sh --host 1.2.3.4 --key ~/.ssh/my-key.pem
#
# Environment variables:
#   EC2_HOST            - EC2 hostname or IP
#   EC2_USER            - SSH user (default: ec2-user)
#   EC2_KEY             - Path to SSH key file
#   ECR_IMAGE           - Full ECR image URI
#
# ==============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --host)
            EC2_HOST="$2"
            shift 2
            ;;
        --user)
            EC2_USER="$2"
            shift 2
            ;;
        --key)
            EC2_KEY="$2"
            shift 2
            ;;
        --image)
            ECR_IMAGE="$2"
            shift 2
            ;;
        --region)
            AWS_REGION="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --host HOST        EC2 hostname or IP address (required)"
            echo "  --user USER        SSH user (default: ec2-user)"
            echo "  --key PATH         Path to SSH private key"
            echo "  --image URI        Full ECR image URI"
            echo "  --region REGION    AWS region (default: us-east-1)"
            echo "  --help             Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Validate required parameters
if [ -z "$EC2_HOST" ]; then
    echo -e "${RED}Error: EC2 host is required. Use --host option.${NC}"
    echo "Example: $0 --host ec2-xx-xx-xx-xx.compute-1.amazonaws.com"
    exit 1
fi

# Default values
EC2_USER="${EC2_USER:-ec2-user}"
AWS_REGION="${AWS_REGION:-us-east-1}"

# Build SSH command
SSH_CMD="ssh"
if [ -n "$EC2_KEY" ]; then
    SSH_CMD="ssh -i $EC2_KEY"
fi
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"

echo "=============================================="
echo "  Owl License Server - EC2 Deployment"
echo "=============================================="
echo ""
echo "Configuration:"
echo "  EC2 Host:   ${EC2_HOST}"
echo "  EC2 User:   ${EC2_USER}"
echo "  AWS Region: ${AWS_REGION}"
if [ -n "$ECR_IMAGE" ]; then
    echo "  Image:      ${ECR_IMAGE}"
fi
echo ""

# Step 1: Test SSH connection
echo -e "${YELLOW}[1/5] Testing SSH connection...${NC}"
if $SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} "echo 'SSH connection successful'" 2>/dev/null; then
    echo -e "${GREEN}  -> Connected${NC}"
else
    echo -e "${RED}  -> Failed to connect to ${EC2_HOST}${NC}"
    exit 1
fi

# Step 2: Check Docker is installed
echo -e "${YELLOW}[2/5] Checking Docker installation...${NC}"
if $SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} "docker --version" 2>/dev/null; then
    echo -e "${GREEN}  -> Docker found${NC}"
else
    echo -e "${RED}  -> Docker not installed. Installing...${NC}"
    $SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} << 'EOF'
        sudo yum update -y
        sudo yum install -y docker
        sudo systemctl start docker
        sudo systemctl enable docker
        sudo usermod -aG docker $USER
EOF
    echo -e "${GREEN}  -> Docker installed${NC}"
fi

# Step 3: Authenticate to ECR on EC2
echo -e "${YELLOW}[3/5] Authenticating to ECR on EC2...${NC}"
$SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} << EOF
    aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin \$(aws sts get-caller-identity --query Account --output text).dkr.ecr.${AWS_REGION}.amazonaws.com
EOF
echo -e "${GREEN}  -> Authenticated${NC}"

# Step 4: Pull latest image
echo -e "${YELLOW}[4/5] Pulling latest image...${NC}"
if [ -n "$ECR_IMAGE" ]; then
    $SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} "docker pull ${ECR_IMAGE}"
    echo -e "${GREEN}  -> Image pulled: ${ECR_IMAGE}${NC}"
else
    echo -e "${YELLOW}  -> No image specified, using existing image${NC}"
fi

# Step 5: Deploy with docker-compose
echo -e "${YELLOW}[5/5] Deploying service...${NC}"

# Copy docker-compose files if they don't exist
$SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} "mkdir -p ~/owl-license-server"

# Create production docker-compose on server
$SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} << EOF
cd ~/owl-license-server

# Stop existing container if running
docker stop owl-license-server 2>/dev/null || true
docker rm owl-license-server 2>/dev/null || true

# Create directories
mkdir -p keys data licenses logs

# Check if secrets.env exists
if [ ! -f secrets.env ]; then
    echo "Creating default secrets.env..."
    cat > secrets.env << 'SECRETS'
# Admin password hash (change this!)
# Generate with: python -c "from werkzeug.security import generate_password_hash; print(generate_password_hash('your-password'))"
OLIB_ADMIN_PASSWORD_HASH=scrypt:32768:8:1\$placeholder\$placeholder

# Webhook API key
OLIB_WEBHOOK_API_KEY=change-this-webhook-key
SECRETS
    echo "WARNING: Please edit secrets.env with your actual secrets!"
fi

# Run container
docker run -d \\
    --name owl-license-server \\
    --restart unless-stopped \\
    -p 3034:3034 \\
    -p 3035:3035 \\
    -v \$(pwd)/keys:/app/keys:ro \\
    -v \$(pwd)/data:/app/data \\
    -v \$(pwd)/licenses:/app/licenses \\
    -v \$(pwd)/logs:/app/logs \\
    -v owl-db-data:/var/lib/postgresql/17/main \\
    --env-file secrets.env \\
    -e OLIB_DB_TYPE=postgresql \\
    -e OLIB_DB_HOST=localhost \\
    -e OLIB_DB_PORT=5432 \\
    -e OLIB_DB_NAME=olib_licenses \\
    -e OLIB_DB_USER=olib \\
    -e OLIB_DB_PASSWORD=olib_secure_password \\
    -e OLIB_ADMIN_HOST=0.0.0.0 \\
    -e OLIB_ADMIN_PORT=3035 \\
    -e OLIB_LICENSE_SERVER_HOST=0.0.0.0 \\
    -e OLIB_LICENSE_SERVER_PORT=3034 \\
    ${ECR_IMAGE:-owl-license-server:latest}

# Wait for container to start
sleep 5

# Check health
docker ps | grep owl-license-server
EOF

echo ""
echo "=============================================="
echo -e "${GREEN}  Deployment Complete!${NC}"
echo "=============================================="
echo ""
echo "Services available at:"
echo "  Admin Panel:     http://${EC2_HOST}:3035"
echo "  Customer Portal: http://${EC2_HOST}:3035/portal"
echo "  License API:     http://${EC2_HOST}:3034"
echo ""
echo "To check logs:"
echo "  $SSH_CMD $SSH_OPTS ${EC2_USER}@${EC2_HOST} 'docker logs -f owl-license-server'"
echo ""
echo "IMPORTANT: Make sure to:"
echo "  1. Copy your RSA keys to ~/owl-license-server/keys/"
echo "  2. Edit ~/owl-license-server/secrets.env with your secrets"
echo "  3. Configure security groups to allow ports 3034 and 3035"
echo ""
