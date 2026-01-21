#!/bin/bash
# =============================================================================
# Push Owl Browser Docker Image to AWS ECR
# =============================================================================
# Usage: ./push-to-ecr.sh [tag]
# Example: ./push-to-ecr.sh v1.0.0
# =============================================================================

set -e

# Configuration (override with environment variables)
AWS_REGION=${AWS_REGION:-us-east-1}
ECR_REPO_NAME=${ECR_REPO_NAME:-olib-browser}
IMAGE_TAG=${1:-latest}
LOCAL_IMAGE=${LOCAL_IMAGE:-olib-browser:latest}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Push to AWS ECR${NC}"
echo -e "${GREEN}========================================${NC}"

# Get AWS Account ID
echo -e "\n${YELLOW}Getting AWS Account ID...${NC}"
AWS_ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text 2>/dev/null)

if [ -z "$AWS_ACCOUNT_ID" ]; then
    echo -e "${RED}Error: Could not get AWS Account ID. Check your AWS credentials.${NC}"
    echo "Run: aws configure"
    exit 1
fi

echo "  Account ID: ${AWS_ACCOUNT_ID}"
echo "  Region: ${AWS_REGION}"
echo "  Repository: ${ECR_REPO_NAME}"
echo "  Tag: ${IMAGE_TAG}"

ECR_URI="${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPO_NAME}"

# Check if local image exists
echo -e "\n${YELLOW}Checking local image...${NC}"
if ! docker image inspect ${LOCAL_IMAGE} > /dev/null 2>&1; then
    echo -e "${RED}Error: Local image '${LOCAL_IMAGE}' not found.${NC}"
    echo "Build it first with: docker build -t olib-browser:latest -f docker/Dockerfile ."
    exit 1
fi
echo "  Found: ${LOCAL_IMAGE}"

# Create repository if it doesn't exist
echo -e "\n${YELLOW}Checking ECR repository...${NC}"
if ! aws ecr describe-repositories --repository-names ${ECR_REPO_NAME} --region ${AWS_REGION} > /dev/null 2>&1; then
    echo "  Repository doesn't exist. Creating..."
    aws ecr create-repository \
        --repository-name ${ECR_REPO_NAME} \
        --region ${AWS_REGION} \
        --image-scanning-configuration scanOnPush=true \
        --encryption-configuration encryptionType=AES256 > /dev/null
    echo -e "  ${GREEN}Created repository: ${ECR_REPO_NAME}${NC}"
else
    echo "  Repository exists: ${ECR_REPO_NAME}"
fi

# Login to ECR
echo -e "\n${YELLOW}Logging into ECR...${NC}"
aws ecr get-login-password --region ${AWS_REGION} | \
    docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com > /dev/null 2>&1
echo -e "  ${GREEN}Login successful${NC}"

# Tag the image
echo -e "\n${YELLOW}Tagging image...${NC}"
docker tag ${LOCAL_IMAGE} ${ECR_URI}:${IMAGE_TAG}
echo "  Tagged: ${ECR_URI}:${IMAGE_TAG}"

# Also tag as latest if not already
if [ "${IMAGE_TAG}" != "latest" ]; then
    docker tag ${LOCAL_IMAGE} ${ECR_URI}:latest
    echo "  Tagged: ${ECR_URI}:latest"
fi

# Push the image
echo -e "\n${YELLOW}Pushing to ECR...${NC}"
docker push ${ECR_URI}:${IMAGE_TAG}

if [ "${IMAGE_TAG}" != "latest" ]; then
    docker push ${ECR_URI}:latest
fi

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}  Push Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Image URI: ${ECR_URI}:${IMAGE_TAG}"
echo ""
echo "To pull this image:"
echo "  aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com"
echo "  docker pull ${ECR_URI}:${IMAGE_TAG}"
