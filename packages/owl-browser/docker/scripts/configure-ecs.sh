#!/bin/bash
# =============================================================================
# Configure ECS Task Definition
# =============================================================================
# This script replaces placeholder values in the ECS task definition
# with your actual AWS account and region information.
#
# Usage: ./configure-ecs.sh [region]
# Example: ./configure-ecs.sh us-east-1
# =============================================================================

set -e

# Configuration
AWS_REGION=${1:-${AWS_REGION:-us-east-1}}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE_FILE="${SCRIPT_DIR}/../ecs/task-definition.json"
OUTPUT_FILE="${SCRIPT_DIR}/../ecs/task-definition-configured.json"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Configure ECS Task Definition${NC}"
echo -e "${GREEN}========================================${NC}"

# Get AWS Account ID
echo -e "\n${YELLOW}Getting AWS Account ID...${NC}"
AWS_ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text 2>/dev/null)

if [ -z "$AWS_ACCOUNT_ID" ]; then
    echo -e "${RED}Error: Could not get AWS Account ID. Check your AWS credentials.${NC}"
    exit 1
fi

echo "  Account ID: ${AWS_ACCOUNT_ID}"
echo "  Region: ${AWS_REGION}"

# Check if template exists
if [ ! -f "$TEMPLATE_FILE" ]; then
    echo -e "${RED}Error: Template file not found: ${TEMPLATE_FILE}${NC}"
    exit 1
fi

# Replace placeholders
echo -e "\n${YELLOW}Configuring task definition...${NC}"
sed -e "s/YOUR_ACCOUNT_ID/${AWS_ACCOUNT_ID}/g" \
    -e "s/YOUR_REGION/${AWS_REGION}/g" \
    "$TEMPLATE_FILE" > "$OUTPUT_FILE"

echo -e "  ${GREEN}Created: ${OUTPUT_FILE}${NC}"

# Create secrets if they don't exist
echo -e "\n${YELLOW}Checking Secrets Manager...${NC}"

create_secret_if_not_exists() {
    local secret_name=$1
    local default_value=$2

    if ! aws secretsmanager describe-secret --secret-id "$secret_name" --region "$AWS_REGION" > /dev/null 2>&1; then
        echo "  Creating secret: $secret_name"
        aws secretsmanager create-secret \
            --name "$secret_name" \
            --secret-string "$default_value" \
            --region "$AWS_REGION" > /dev/null
        echo -e "  ${GREEN}Created: $secret_name${NC}"
        echo -e "  ${YELLOW}WARNING: Update this secret with a secure value!${NC}"
    else
        echo "  Secret exists: $secret_name"
    fi
}

# Create secrets with placeholder values
create_secret_if_not_exists "owl-browser/http-token" "$(openssl rand -hex 32 2>/dev/null || echo 'change-me-http-token')"
create_secret_if_not_exists "owl-browser/panel-password" "$(openssl rand -hex 16 2>/dev/null || echo 'change-me-password')"

# Create CloudWatch log group
echo -e "\n${YELLOW}Checking CloudWatch log group...${NC}"
if ! aws logs describe-log-groups --log-group-name-prefix "/ecs/owl-browser" --region "$AWS_REGION" --query 'logGroups[0].logGroupName' --output text 2>/dev/null | grep -q "/ecs/owl-browser"; then
    echo "  Creating log group: /ecs/owl-browser"
    aws logs create-log-group --log-group-name "/ecs/owl-browser" --region "$AWS_REGION"
    aws logs put-retention-policy --log-group-name "/ecs/owl-browser" --retention-in-days 30 --region "$AWS_REGION"
    echo -e "  ${GREEN}Created log group with 30-day retention${NC}"
else
    echo "  Log group exists: /ecs/owl-browser"
fi

# Check for required IAM roles
echo -e "\n${YELLOW}Checking IAM roles...${NC}"
check_role() {
    local role_name=$1
    if aws iam get-role --role-name "$role_name" > /dev/null 2>&1; then
        echo "  Role exists: $role_name"
        return 0
    else
        echo -e "  ${RED}Role missing: $role_name${NC}"
        return 1
    fi
}

ROLES_OK=true
check_role "ecsTaskExecutionRole" || ROLES_OK=false
check_role "ecsTaskRole" || ROLES_OK=false

if [ "$ROLES_OK" = false ]; then
    echo -e "\n${YELLOW}To create missing roles, run:${NC}"
    echo ""
    echo "# Create ecsTaskExecutionRole"
    echo "aws iam create-role --role-name ecsTaskExecutionRole --assume-role-policy-document '{\"Version\":\"2012-10-17\",\"Statement\":[{\"Effect\":\"Allow\",\"Principal\":{\"Service\":\"ecs-tasks.amazonaws.com\"},\"Action\":\"sts:AssumeRole\"}]}'"
    echo "aws iam attach-role-policy --role-name ecsTaskExecutionRole --policy-arn arn:aws:iam::aws:policy/service-role/AmazonECSTaskExecutionRolePolicy"
    echo "aws iam attach-role-policy --role-name ecsTaskExecutionRole --policy-arn arn:aws:iam::aws:policy/SecretsManagerReadWrite"
    echo ""
    echo "# Create ecsTaskRole"
    echo "aws iam create-role --role-name ecsTaskRole --assume-role-policy-document '{\"Version\":\"2012-10-17\",\"Statement\":[{\"Effect\":\"Allow\",\"Principal\":{\"Service\":\"ecs-tasks.amazonaws.com\"},\"Action\":\"sts:AssumeRole\"}]}'"
fi

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}  Configuration Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Next steps:"
echo ""
echo "1. Review the configured task definition:"
echo "   cat ${OUTPUT_FILE}"
echo ""
echo "2. Register the task definition:"
echo "   aws ecs register-task-definition --cli-input-json file://${OUTPUT_FILE} --region ${AWS_REGION}"
echo ""
echo "3. Create an ECS cluster (if not exists):"
echo "   aws ecs create-cluster --cluster-name owl-browser-cluster --region ${AWS_REGION}"
echo ""
echo "4. Create an ECS service:"
echo "   aws ecs create-service \\"
echo "     --cluster owl-browser-cluster \\"
echo "     --service-name owl-browser-service \\"
echo "     --task-definition owl-browser \\"
echo "     --desired-count 1 \\"
echo "     --launch-type FARGATE \\"
echo "     --network-configuration 'awsvpcConfiguration={subnets=[subnet-xxx],securityGroups=[sg-xxx],assignPublicIp=ENABLED}' \\"
echo "     --region ${AWS_REGION}"
