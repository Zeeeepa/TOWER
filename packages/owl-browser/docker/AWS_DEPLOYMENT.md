# AWS Deployment Guide for Owl Browser

This guide covers publishing the Owl Browser Docker image to AWS ECR and deploying it for maximum performance.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Building the Docker Image](#building-the-docker-image)
3. [Publishing to AWS ECR](#publishing-to-aws-ecr)
4. [Deployment Options](#deployment-options)
   - [Option A: EC2 Direct (Recommended for Raw Power)](#option-a-ec2-direct-recommended)
   - [Option B: ECS on EC2 (Recommended for Orchestration)](#option-b-ecs-on-ec2)
5. [Environment Variables](#environment-variables)
6. [Security Best Practices](#security-best-practices)
7. [Monitoring & Health Checks](#monitoring--health-checks)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Local Machine Requirements

```bash
# Install AWS CLI v2
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip awscliv2.zip
sudo ./aws/install

# Verify installation
aws --version

# Configure AWS credentials
aws configure
# Enter: AWS Access Key ID, Secret Access Key, Region (e.g., us-east-1)
```

### Required IAM Permissions

Your AWS user/role needs these permissions:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "ecr:GetAuthorizationToken",
        "ecr:BatchCheckLayerAvailability",
        "ecr:GetDownloadUrlForLayer",
        "ecr:BatchGetImage",
        "ecr:PutImage",
        "ecr:InitiateLayerUpload",
        "ecr:UploadLayerPart",
        "ecr:CompleteLayerUpload",
        "ecr:CreateRepository",
        "ecr:DescribeRepositories"
      ],
      "Resource": "*"
    }
  ]
}
```

---

## Building the Docker Image

### Step 1: Set Build Arguments

Create a `.env.build` file (DO NOT commit this file):

```bash
# .env.build
OWL_LICENSE_SERVER_URL=https://license.owlbrowser.net
OWL_NONCE_HMAC_SECRET=your-hmac-secret-here
OWL_VM_PROFILE_DB_PASS=your-database-password-here
```

### Step 2: Build the Image

```bash
# Navigate to project root
cd /path/to/olib-browser

# Build the image (this takes 10-20 minutes)
docker build \
  --build-arg OWL_LICENSE_SERVER_URL=https://license.owlbrowser.net \
  --build-arg OWL_NONCE_HMAC_SECRET=your-hmac-secret \
  --build-arg OWL_VM_PROFILE_DB_PASS=your-db-password \
  -t olib-browser:latest \
  -f docker/Dockerfile \
  .

# Verify the image was built
docker images | grep olib-browser
```

### Step 3: Test Locally (Optional)

```bash
# Run locally to verify
docker run -d \
  --name owl-test \
  -p 80:80 \
  -p 8080:8080 \
  -e OWL_HTTP_TOKEN=test-token \
  -e OWL_PANEL_PASSWORD=test-password \
  --shm-size=2g \
  olib-browser:latest

# Check health
curl http://localhost:8080/health

# View logs
docker logs owl-test

# Cleanup
docker stop owl-test && docker rm owl-test
```

---

## Publishing to AWS ECR

### Step 1: Create ECR Repository

```bash
# Set your AWS region
export AWS_REGION=us-east-1
export AWS_ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
export ECR_REPO_NAME=olib-browser

# Create the repository
aws ecr create-repository \
  --repository-name ${ECR_REPO_NAME} \
  --region ${AWS_REGION} \
  --image-scanning-configuration scanOnPush=true \
  --encryption-configuration encryptionType=AES256

# Output: Note the repositoryUri
```

### Step 2: Authenticate Docker with ECR

```bash
# Login to ECR (valid for 12 hours)
aws ecr get-login-password --region ${AWS_REGION} | \
  docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com
```

### Step 3: Tag and Push the Image

```bash
# Tag the image for ECR
docker tag olib-browser:latest \
  ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPO_NAME}:latest

# Also tag with version
docker tag olib-browser:latest \
  ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPO_NAME}:v1.0.0

# Push to ECR
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPO_NAME}:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPO_NAME}:v1.0.0

# Verify push
aws ecr describe-images --repository-name ${ECR_REPO_NAME} --region ${AWS_REGION}
```

### Quick Push Script

Save as `docker/scripts/push-to-ecr.sh`:

```bash
#!/bin/bash
set -e

AWS_REGION=${AWS_REGION:-us-east-1}
AWS_ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
ECR_REPO_NAME=${ECR_REPO_NAME:-olib-browser}
IMAGE_TAG=${1:-latest}

ECR_URI="${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/${ECR_REPO_NAME}"

echo "Logging into ECR..."
aws ecr get-login-password --region ${AWS_REGION} | \
  docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com

echo "Tagging image..."
docker tag olib-browser:latest ${ECR_URI}:${IMAGE_TAG}

echo "Pushing to ECR..."
docker push ${ECR_URI}:${IMAGE_TAG}

echo "Done! Image pushed to: ${ECR_URI}:${IMAGE_TAG}"
```

---

## Deployment Options

### Option A: EC2 Direct (Recommended)

Best for **maximum raw power** with 100% CPU/memory access.

#### Instance Type Selection

| Workload | Instance | vCPU | Memory | Cost/hr* |
|----------|----------|------|--------|----------|
| Light (1-5 browsers) | c7i.xlarge | 4 | 8 GB | ~$0.18 |
| Medium (5-15 browsers) | c7i.2xlarge | 8 | 16 GB | ~$0.36 |
| Heavy (15-30 browsers) | c7i.4xlarge | 16 | 32 GB | ~$0.71 |
| Large (30-60 browsers) | c7i.8xlarge | 32 | 64 GB | ~$1.43 |
| Maximum | c7i.metal | 128 | 256 GB | ~$5.71 |

*Prices are approximate for us-east-1, on-demand.

#### Step 1: Launch EC2 Instance

```bash
# Create a key pair (if you don't have one)
aws ec2 create-key-pair \
  --key-name owl-browser-key \
  --query 'KeyMaterial' \
  --output text > owl-browser-key.pem
chmod 400 owl-browser-key.pem

# Create security group
aws ec2 create-security-group \
  --group-name owl-browser-sg \
  --description "Owl Browser security group"

# Get security group ID
SG_ID=$(aws ec2 describe-security-groups \
  --group-names owl-browser-sg \
  --query 'SecurityGroups[0].GroupId' \
  --output text)

# Allow SSH (port 22), HTTP (port 80), and API (port 8080)
aws ec2 authorize-security-group-ingress --group-id $SG_ID --protocol tcp --port 22 --cidr 0.0.0.0/0
aws ec2 authorize-security-group-ingress --group-id $SG_ID --protocol tcp --port 80 --cidr 0.0.0.0/0
aws ec2 authorize-security-group-ingress --group-id $SG_ID --protocol tcp --port 8080 --cidr 0.0.0.0/0

# Launch instance (Ubuntu 22.04 AMI)
aws ec2 run-instances \
  --image-id ami-0c7217cdde317cfec \
  --instance-type c7i.2xlarge \
  --key-name owl-browser-key \
  --security-group-ids $SG_ID \
  --block-device-mappings '[{"DeviceName":"/dev/sda1","Ebs":{"VolumeSize":50,"VolumeType":"gp3"}}]' \
  --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=owl-browser}]' \
  --user-data file://docker/scripts/ec2-user-data.sh
```

#### Step 2: EC2 User Data Script

Save as `docker/scripts/ec2-user-data.sh`:

```bash
#!/bin/bash
set -e

# Update system
apt-get update
apt-get upgrade -y

# Install Docker
apt-get install -y docker.io
systemctl enable docker
systemctl start docker

# Install AWS CLI
apt-get install -y unzip curl
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip awscliv2.zip
./aws/install

# Add ubuntu user to docker group
usermod -aG docker ubuntu

# Create app directory
mkdir -p /opt/owl-browser
cd /opt/owl-browser

# Create docker-compose.yml
cat > docker-compose.yml << 'EOF'
version: '3.8'
services:
  owl-browser:
    image: ${ECR_IMAGE}
    container_name: owl-browser
    restart: unless-stopped
    ports:
      - "80:80"
      - "8080:8080"
    environment:
      - OWL_HTTP_TOKEN=${OWL_HTTP_TOKEN}
      - OWL_PANEL_PASSWORD=${OWL_PANEL_PASSWORD}
      - OWL_HTTP_MAX_CONNECTIONS=100
      - OWL_BROWSER_TIMEOUT=60000
    shm_size: '2gb'
    deploy:
      resources:
        limits:
          memory: 14G
        reservations:
          memory: 8G
EOF

# Create environment file template
cat > .env.template << 'EOF'
ECR_IMAGE=YOUR_ACCOUNT_ID.dkr.ecr.YOUR_REGION.amazonaws.com/olib-browser:latest
OWL_HTTP_TOKEN=your-secure-token-here
OWL_PANEL_PASSWORD=your-secure-password-here
EOF

echo "Setup complete! Configure /opt/owl-browser/.env and run: docker-compose up -d"
```

#### Step 3: Connect and Deploy

```bash
# Get instance public IP
INSTANCE_IP=$(aws ec2 describe-instances \
  --filters "Name=tag:Name,Values=owl-browser" "Name=instance-state-name,Values=running" \
  --query 'Reservations[0].Instances[0].PublicIpAddress' \
  --output text)

# SSH into instance
ssh -i owl-browser-key.pem ubuntu@${INSTANCE_IP}

# On the EC2 instance:
cd /opt/owl-browser

# Login to ECR
aws ecr get-login-password --region us-east-1 | \
  docker login --username AWS --password-stdin YOUR_ACCOUNT_ID.dkr.ecr.us-east-1.amazonaws.com

# Create .env file
cp .env.template .env
nano .env  # Edit with your values

# Pull and run
docker-compose pull
docker-compose up -d

# Check status
docker-compose ps
docker-compose logs -f
```

---

### Option B: ECS on EC2

Best for **orchestration with raw power** - auto-restart, load balancing, scaling.

#### Step 1: Create ECS Cluster

```bash
# Create cluster with EC2 capacity
aws ecs create-cluster \
  --cluster-name owl-browser-cluster \
  --capacity-providers FARGATE FARGATE_SPOT \
  --default-capacity-provider-strategy capacityProvider=FARGATE,weight=1
```

#### Step 2: Create Task Definition

Save as `docker/ecs/task-definition.json`:

```json
{
  "family": "owl-browser",
  "networkMode": "awsvpc",
  "requiresCompatibilities": ["EC2"],
  "cpu": "4096",
  "memory": "8192",
  "executionRoleArn": "arn:aws:iam::YOUR_ACCOUNT_ID:role/ecsTaskExecutionRole",
  "containerDefinitions": [
    {
      "name": "owl-browser",
      "image": "YOUR_ACCOUNT_ID.dkr.ecr.us-east-1.amazonaws.com/olib-browser:latest",
      "essential": true,
      "portMappings": [
        {
          "containerPort": 80,
          "hostPort": 80,
          "protocol": "tcp"
        },
        {
          "containerPort": 8080,
          "hostPort": 8080,
          "protocol": "tcp"
        }
      ],
      "environment": [
        {"name": "OWL_HTTP_MAX_CONNECTIONS", "value": "100"},
        {"name": "OWL_BROWSER_TIMEOUT", "value": "60000"}
      ],
      "secrets": [
        {
          "name": "OWL_HTTP_TOKEN",
          "valueFrom": "arn:aws:secretsmanager:us-east-1:YOUR_ACCOUNT_ID:secret:owl-browser/http-token"
        },
        {
          "name": "OWL_PANEL_PASSWORD",
          "valueFrom": "arn:aws:secretsmanager:us-east-1:YOUR_ACCOUNT_ID:secret:owl-browser/panel-password"
        }
      ],
      "linuxParameters": {
        "sharedMemorySize": 2048
      },
      "logConfiguration": {
        "logDriver": "awslogs",
        "options": {
          "awslogs-group": "/ecs/owl-browser",
          "awslogs-region": "us-east-1",
          "awslogs-stream-prefix": "ecs"
        }
      },
      "healthCheck": {
        "command": ["CMD-SHELL", "curl -f http://localhost:8080/health || exit 1"],
        "interval": 30,
        "timeout": 10,
        "retries": 3,
        "startPeriod": 60
      }
    }
  ]
}
```

#### Step 3: Create Secrets in AWS Secrets Manager

```bash
# Create secrets for sensitive values
aws secretsmanager create-secret \
  --name owl-browser/http-token \
  --secret-string "your-secure-http-token"

aws secretsmanager create-secret \
  --name owl-browser/panel-password \
  --secret-string "your-secure-panel-password"
```

#### Step 4: Register Task Definition

```bash
aws ecs register-task-definition --cli-input-json file://docker/ecs/task-definition.json
```

#### Step 5: Create Service

```bash
aws ecs create-service \
  --cluster owl-browser-cluster \
  --service-name owl-browser-service \
  --task-definition owl-browser \
  --desired-count 1 \
  --launch-type EC2 \
  --network-configuration "awsvpcConfiguration={subnets=[subnet-xxx],securityGroups=[sg-xxx],assignPublicIp=ENABLED}"
```

---

## Environment Variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `OWL_HTTP_TOKEN` | Yes | - | API authentication token |
| `OWL_PANEL_PASSWORD` | Yes | changeme | Web panel login password |
| `OWL_HTTP_HOST` | No | 0.0.0.0 | HTTP server bind address |
| `OWL_HTTP_PORT` | No | 8080 | HTTP server port |
| `OWL_HTTP_MAX_CONNECTIONS` | No | 100 | Max concurrent connections |
| `OWL_HTTP_TIMEOUT` | No | 30000 | HTTP request timeout (ms) |
| `OWL_BROWSER_TIMEOUT` | No | 60000 | Browser operation timeout (ms) |
| `OWL_RATE_LIMIT_ENABLED` | No | false | Enable rate limiting |
| `OWL_RATE_LIMIT_REQUESTS` | No | 100 | Requests per window |
| `OWL_RATE_LIMIT_WINDOW` | No | 60 | Rate limit window (seconds) |
| `OWL_CORS_ENABLED` | No | true | Enable CORS |
| `OWL_CORS_ORIGINS` | No | * | Allowed origins |

---

## Security Best Practices

### 1. Use Strong Tokens

```bash
# Generate secure token
openssl rand -hex 32
```

### 2. Restrict Security Group

```bash
# Only allow traffic from specific IPs
aws ec2 authorize-security-group-ingress \
  --group-id $SG_ID \
  --protocol tcp \
  --port 80 \
  --cidr YOUR_OFFICE_IP/32
```

### 3. Use AWS Secrets Manager

Never hardcode secrets. Use Secrets Manager or Parameter Store:

```bash
# Store secret
aws secretsmanager create-secret \
  --name owl-browser/config \
  --secret-string '{"token":"xxx","password":"yyy"}'

# Retrieve in script
aws secretsmanager get-secret-value \
  --secret-id owl-browser/config \
  --query SecretString --output text | jq -r '.token'
```

### 4. Enable VPC Endpoints

For private ECR access without internet:

```bash
aws ec2 create-vpc-endpoint \
  --vpc-id vpc-xxx \
  --service-name com.amazonaws.us-east-1.ecr.dkr \
  --vpc-endpoint-type Interface
```

### 5. Use IAM Roles (Not Access Keys)

Attach IAM role to EC2 instance instead of using access keys:

```bash
aws ec2 associate-iam-instance-profile \
  --instance-id i-xxx \
  --iam-instance-profile Name=owl-browser-role
```

---

## Monitoring & Health Checks

### CloudWatch Metrics

```bash
# Create CloudWatch log group
aws logs create-log-group --log-group-name /owl-browser/application

# View logs
aws logs tail /owl-browser/application --follow
```

### Health Check Endpoints

```bash
# Basic health
curl http://YOUR_IP:8080/health

# Detailed status (requires token)
curl -H "Authorization: Bearer YOUR_TOKEN" http://YOUR_IP:8080/status
```

### CloudWatch Alarms

```bash
# CPU alarm
aws cloudwatch put-metric-alarm \
  --alarm-name owl-browser-high-cpu \
  --metric-name CPUUtilization \
  --namespace AWS/EC2 \
  --statistic Average \
  --period 300 \
  --threshold 80 \
  --comparison-operator GreaterThanThreshold \
  --dimensions Name=InstanceId,Value=i-xxx \
  --evaluation-periods 2 \
  --alarm-actions arn:aws:sns:us-east-1:xxx:alerts
```

---

## Troubleshooting

### Common Issues

#### 1. Container won't start

```bash
# Check logs
docker logs owl-browser

# Common fix: increase shared memory
docker run --shm-size=2g ...
```

#### 2. ECR authentication fails

```bash
# Re-authenticate (token expires after 12 hours)
aws ecr get-login-password --region us-east-1 | \
  docker login --username AWS --password-stdin xxx.dkr.ecr.us-east-1.amazonaws.com
```

#### 3. Out of memory

```bash
# Check memory usage
docker stats owl-browser

# Increase instance size or limit concurrent browsers
OWL_HTTP_MAX_CONNECTIONS=50
```

#### 4. Slow performance

```bash
# Use compute-optimized instances (c7i family)
# Enable enhanced networking
aws ec2 modify-instance-attribute \
  --instance-id i-xxx \
  --ena-support
```

### Useful Commands

```bash
# View running containers
docker ps

# Enter container shell
docker exec -it owl-browser /bin/bash

# View resource usage
docker stats

# Restart container
docker-compose restart

# Pull latest image
docker-compose pull && docker-compose up -d

# View all logs
docker-compose logs -f --tail=100
```

---

## Cost Optimization

### Use Spot Instances (Up to 90% savings)

```bash
aws ec2 run-instances \
  --instance-type c7i.2xlarge \
  --instance-market-options 'MarketType=spot,SpotOptions={SpotInstanceType=persistent}'
```

### Reserved Instances (Up to 72% savings)

For predictable workloads, purchase 1-year or 3-year reserved capacity.

### Auto-Scaling

Scale down during off-hours:

```bash
# Create scaling schedule
aws autoscaling put-scheduled-action \
  --auto-scaling-group-name owl-browser-asg \
  --scheduled-action-name scale-down-night \
  --recurrence "0 22 * * *" \
  --desired-capacity 0
```

---

## Quick Reference

```bash
# Build
docker build --build-arg OWL_VM_PROFILE_DB_PASS=xxx -t olib-browser:latest -f docker/Dockerfile .

# Push to ECR
aws ecr get-login-password | docker login --username AWS --password-stdin $ECR_URI
docker tag olib-browser:latest $ECR_URI:latest
docker push $ECR_URI:latest

# Deploy on EC2
docker run -d --name owl -p 80:80 -p 8080:8080 --shm-size=2g \
  -e OWL_HTTP_TOKEN=xxx -e OWL_PANEL_PASSWORD=xxx \
  $ECR_URI:latest

# Health check
curl http://localhost:8080/health
```
