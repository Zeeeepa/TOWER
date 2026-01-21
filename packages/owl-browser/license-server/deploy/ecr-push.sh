#!/bin/bash
# ==============================================================================
# Push Owl License Server Docker Image to AWS ECR
# ==============================================================================
#
# Prerequisites:
#   - AWS CLI installed and configured
#   - Docker installed
#   - ECR repository created
#
# Usage:
#   ./deploy/ecr-push.sh                    # Uses defaults
#   ./deploy/ecr-push.sh --region us-west-2 # Specify region
#
# Environment variables (optional):
#   AWS_REGION          - AWS region (default: us-east-1)
#   AWS_ACCOUNT_ID      - AWS account ID (auto-detected if not set)
#   ECR_REPO_NAME       - ECR repository name (default: owl-license-server)
#   IMAGE_TAG           - Docker image tag (default: latest)
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
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --region)
            AWS_REGION="$2"
            shift 2
            ;;
        --account)
            AWS_ACCOUNT_ID="$2"
            shift 2
            ;;
        --repo)
            ECR_REPO_NAME="$2"
            shift 2
            ;;
        --tag)
            IMAGE_TAG="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --region REGION    AWS region (default: us-east-1)"
            echo "  --account ID       AWS account ID (auto-detected if not set)"
            echo "  --repo NAME        ECR repository name (default: owl-license-server)"
            echo "  --tag TAG          Docker image tag (default: latest)"
            echo "  --help             Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Default values
AWS_REGION="${AWS_REGION:-us-east-1}"
ECR_REPO_NAME="${ECR_REPO_NAME:-owl-license-server}"
IMAGE_TAG="${IMAGE_TAG:-latest}"

# Auto-detect AWS account ID if not set
if [ -z "$AWS_ACCOUNT_ID" ]; then
    echo -e "${YELLOW}Detecting AWS Account ID...${NC}"
    AWS_ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
    if [ -z "$AWS_ACCOUNT_ID" ]; then
        echo -e "${RED}Failed to detect AWS Account ID. Please set AWS_ACCOUNT_ID environment variable.${NC}"
        exit 1
    fi
fi

# ECR registry URL
ECR_REGISTRY="${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com"
ECR_IMAGE="${ECR_REGISTRY}/${ECR_REPO_NAME}:${IMAGE_TAG}"

echo "=============================================="
echo "  Owl License Server - ECR Push"
echo "=============================================="
echo ""
echo "Configuration:"
echo "  AWS Region:     ${AWS_REGION}"
echo "  AWS Account:    ${AWS_ACCOUNT_ID}"
echo "  ECR Repository: ${ECR_REPO_NAME}"
echo "  Image Tag:      ${IMAGE_TAG}"
echo "  Full Image:     ${ECR_IMAGE}"
echo ""

# Step 1: Check if ECR repository exists, create if not
echo -e "${YELLOW}[1/5] Checking ECR repository...${NC}"
if aws ecr describe-repositories --repository-names "$ECR_REPO_NAME" --region "$AWS_REGION" >/dev/null 2>&1; then
    echo -e "${GREEN}  -> Repository exists${NC}"
else
    echo "  -> Creating ECR repository..."
    aws ecr create-repository \
        --repository-name "$ECR_REPO_NAME" \
        --region "$AWS_REGION" \
        --image-scanning-configuration scanOnPush=true \
        --encryption-configuration encryptionType=AES256
    echo -e "${GREEN}  -> Repository created${NC}"
fi

# Step 2: Authenticate Docker to ECR
echo -e "${YELLOW}[2/5] Authenticating Docker to ECR...${NC}"
aws ecr get-login-password --region "$AWS_REGION" | \
    docker login --username AWS --password-stdin "$ECR_REGISTRY"
echo -e "${GREEN}  -> Authenticated${NC}"

# Step 3: Build Docker image
echo -e "${YELLOW}[3/5] Building Docker image...${NC}"
cd "$PROJECT_DIR"

# Run the build script if it exists
if [ -f "./build-docker.sh" ]; then
    ./build-docker.sh
else
    docker build -t owl-license-server:latest .
fi
echo -e "${GREEN}  -> Image built${NC}"

# Step 4: Tag image for ECR
echo -e "${YELLOW}[4/5] Tagging image for ECR...${NC}"
docker tag owl-license-server:latest "$ECR_IMAGE"

# Also tag with git commit hash if available
if git rev-parse --short HEAD >/dev/null 2>&1; then
    GIT_HASH=$(git rev-parse --short HEAD)
    ECR_IMAGE_HASH="${ECR_REGISTRY}/${ECR_REPO_NAME}:${GIT_HASH}"
    docker tag owl-license-server:latest "$ECR_IMAGE_HASH"
    echo "  -> Tagged: ${ECR_IMAGE_HASH}"
fi
echo -e "${GREEN}  -> Tagged: ${ECR_IMAGE}${NC}"

# Step 5: Push to ECR
echo -e "${YELLOW}[5/5] Pushing to ECR...${NC}"
docker push "$ECR_IMAGE"

if [ -n "$GIT_HASH" ]; then
    docker push "$ECR_IMAGE_HASH"
fi

echo ""
echo "=============================================="
echo -e "${GREEN}  Push Complete!${NC}"
echo "=============================================="
echo ""
echo "Image URI: ${ECR_IMAGE}"
if [ -n "$GIT_HASH" ]; then
    echo "Git Hash:  ${ECR_IMAGE_HASH}"
fi
echo ""
echo "To deploy on EC2, run:"
echo "  ./deploy/ec2-deploy.sh --image ${ECR_IMAGE}"
echo ""
