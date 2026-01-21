# Owl Browser License Server

A comprehensive license management system for Owl Browser with:
- **Admin Server** - Beautiful web UI for license management (port 3035)
- **Customer Portal** - Self-service portal for customers at `/portal` (port 3035)
- **Subscription Server** - REST API for license activation and subscription validation (port 3034)
- **Seat Management** - Track and enforce device limits per license
- **RSA-Signed Responses** - All server responses are cryptographically signed
- **HMAC-Authenticated Requests** - Mutual authentication prevents MITM attacks
- **Multi-Database Support** - SQLite, PostgreSQL, or MySQL
- **Docker Support** - One-command deployment with PostgreSQL 17

## Quick Start

### Option A: Docker Deployment (Recommended for Production)

The easiest way to deploy is using Docker, which includes PostgreSQL 17 and all dependencies:

```bash
cd license-server

# 1. Copy your RSA keys (must match keys used to build the browser)
mkdir -p keys
cp ~/.owl_license/owl_license.key keys/private_key.pem
cp ~/.owl_license/owl_license.pub keys/public_key.pem

# 2. Configure environment
cp .env.example .env
# Edit .env with your settings (passwords, secrets, etc.)

# 3. Build and run
./build-docker.sh
docker-compose up -d

# 4. View logs
docker-compose logs -f
```

Services will be available at:
- **Admin Server**: http://localhost:3035
- **Customer Portal**: http://localhost:3035/portal
- **License API**: http://localhost:3034

### Option B: Local Development

### 1. Install Dependencies

```bash
cd license-server
pip install -r requirements.txt
```

### 2. Build the License Generator

```bash
cd ..
npm run build
# or manually: cd build && cmake .. && make license_generator
```

### 3. Configure Environment

Edit the `.env` file in the project root:

```bash
# Generate a password hash
python -c "from werkzeug.security import generate_password_hash; print(generate_password_hash('your-secure-password'))"

# Generate secret keys
python -c "import secrets; print(secrets.token_hex(32))"
```

Update `.env`:
```env
# Admin authentication
OLIB_ADMIN_PASSWORD_HASH=scrypt:32768:8:1$...your-hash...
OLIB_ADMIN_SECRET_KEY=your-generated-secret

# Webhook API key (for payment provider integration)
OLIB_WEBHOOK_API_KEY=your-webhook-key

# HMAC secret for request authentication (MUST match browser build)
OWL_NONCE_HMAC_SECRET=your-hmac-secret
```

**Important:** The `OWL_NONCE_HMAC_SECRET` is compiled into the browser binary during build. If you change it, you must rebuild the browser.

### 4. Set Up RSA Keys

The server uses RSA keys to sign responses. Place your keys in `~/.owl_license/`:

```bash
# Keys should be at:
~/.owl_license/owl_license.key  # Private key (for signing)
~/.owl_license/owl_license.pub  # Public key (embedded in browser)
```

Or fallback location in `license-server/keys/`:
```bash
license-server/keys/private_key.pem
license-server/keys/public_key.pem
```

### 5. Start the Servers

**Admin Server** (port 3035):
```bash
python admin_server.py
```

**Subscription Server** (port 3034):
```bash
python subscription_server.py
```

### 6. Access the Admin UI

Open http://127.0.0.1:3035 in your browser.

Default credentials (change in production!):
- Username: `admin`
- Password: `admin`

## Architecture

```
license-server/
├── admin_server.py          # Admin web interface (port 3035)
├── subscription_server.py   # License activation & subscription API (port 3034)
├── customer_portal.py       # Customer self-service portal (Blueprint)
├── config.py                # Centralized configuration
├── database.py              # Database abstraction layer
├── init_database.py         # Database schema initialization
├── requirements.txt         # Python dependencies
│
├── templates/               # HTML templates (Jinja2)
│   ├── base.html           # Dark-themed base layout
│   ├── login.html          # Admin login page
│   ├── dashboard.html      # Statistics dashboard
│   ├── licenses.html       # License list
│   ├── license_form.html   # Create/edit license
│   ├── license_detail.html # License details + seats
│   ├── subscriptions.html  # Subscription management
│   ├── activations.html    # Activation logs
│   ├── seats.html          # Active seats overview
│   ├── audit_log.html      # Admin audit trail
│   └── customer/           # Customer portal templates
│       ├── base.html       # Customer portal layout
│       ├── login.html      # Customer login
│       ├── register.html   # Registration (license + email)
│       ├── dashboard.html  # Customer dashboard
│       ├── licenses.html   # View licenses
│       ├── profile.html    # Account settings
│       └── billing.html    # Invoice history
│
├── docker/                  # Docker configuration
│   ├── supervisord.conf    # Process management
│   └── entrypoint.sh       # Container initialization
├── Dockerfile              # Docker image definition
├── docker-compose.yml      # Production deployment
├── docker-compose.dev.yml  # Development override (live code updates)
├── build-docker.sh         # Build script
├── dev-reload.sh           # Reload servers in dev mode
├── .env.example            # Environment template
│
├── deploy/                  # AWS deployment scripts
│   ├── ecr-push.sh         # Push image to AWS ECR
│   ├── ec2-deploy.sh       # Deploy to EC2 instance
│   └── ec2-userdata.sh     # EC2 initial setup script
│
├── data/                    # SQLite database (gitignored)
├── licenses/                # Generated license files (gitignored)
├── keys/                    # RSA keys for signing (gitignored)
└── logs/                    # Application logs (gitignored)
```

## Database Schema

### Licenses Table
Stores all license information including type, seats, expiry, etc.

### Subscriptions Table
Tracks subscription-specific data (activation date, check intervals, grace period).

### License Seats Table
Tracks active devices per license:
```sql
CREATE TABLE license_seats (
    id INTEGER PRIMARY KEY,
    license_id TEXT NOT NULL,
    hardware_fingerprint TEXT NOT NULL,
    first_activated_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    ip_address TEXT,
    user_agent TEXT,
    is_active INTEGER DEFAULT 1,
    deactivated_at TEXT,
    UNIQUE(license_id, hardware_fingerprint)
);
```

### Activation Logs Table
Records all activation attempts (success and failure) for audit purposes.

## Database Configuration

### SQLite (Default)

SQLite is used by default for development. The database file is created at `data/licenses.db`.

### PostgreSQL (Production)

For production, switch to PostgreSQL:

```env
OLIB_DB_TYPE=postgresql
OLIB_DB_HOST=localhost
OLIB_DB_PORT=5432
OLIB_DB_NAME=olib_licenses
OLIB_DB_USER=olib
OLIB_DB_PASSWORD=your-password
```

Install the driver:
```bash
pip install psycopg2-binary
```

### MySQL

```env
OLIB_DB_TYPE=mysql
OLIB_DB_HOST=localhost
OLIB_DB_PORT=3306
OLIB_DB_NAME=olib_licenses
OLIB_DB_USER=olib
OLIB_DB_PASSWORD=your-password
```

Install the driver:
```bash
pip install mysql-connector-python
```

## API Endpoints

### Subscription Server (port 3034)

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/v1/health` | Health check |
| POST | `/api/v1/license/activate` | **Universal license activation (ALL types)** |
| POST | `/api/v1/subscription/check` | Check subscription status |
| POST | `/api/v1/subscription/activate` | Activate subscription (legacy, redirects to universal) |
| GET | `/api/v1/license/info` | Get license info |

### Universal License Activation

**All license types** (not just subscriptions) must call this endpoint when activated. This enables:
- Seat tracking based on hardware fingerprint
- Max seats enforcement
- Activation logging for audit trails

**Request:**
```http
POST /api/v1/license/activate
Content-Type: application/json
X-Nonce: <random_hex>.<timestamp>.<hmac_hex>

{
    "license_id": "uuid",
    "hardware_fingerprint": "sha256-hash"
}
```

**Response (Success):**
```json
{
    "license_id": "uuid",
    "activated": true,
    "seat_count": 1,
    "max_seats": 3,
    "license_type": 2,
    "license_type_name": "BUSINESS",
    "activation_timestamp": 1764364494,
    "server_timestamp": 1764364494,
    "signature": "base64-rsa-signature"
}
```

**Response (Max Seats Exceeded):**
```json
{
    "license_id": "uuid",
    "activated": false,
    "seat_count": 3,
    "max_seats": 3,
    "error": "Maximum seats (3) reached",
    "signature": "base64-rsa-signature"
}
```

### HMAC-Authenticated Nonces

All requests should include an `X-Nonce` header for replay protection and mutual authentication:

```
X-Nonce: <random_hex>.<timestamp>.<hmac_hex>
```

Format:
- `random_hex`: 32 characters (16 bytes hex-encoded)
- `timestamp`: Unix timestamp (decimal)
- `hmac_hex`: HMAC-SHA256 of `random_hex + timestamp + license_id` using shared secret

The server verifies:
1. HMAC matches expected value
2. Timestamp is within 5-minute tolerance

### RSA-Signed Responses

All responses include a `signature` field containing a Base64-encoded RSA signature of the response JSON (excluding the signature field). The browser verifies this signature using the embedded public key to prevent MITM attacks.

### Admin Server (port 3035)

The admin server provides a web UI. API endpoints are also available:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/stats` | Dashboard statistics |
| GET | `/api/licenses/{id}/verify` | Verify license file |
| GET | `/activations` | View recent activations |
| GET | `/seats` | View all active seats |
| POST | `/licenses/{id}/seats/{hw}/deactivate` | Deactivate a seat |

## License Types

| Type | ID | Description | Pricing |
|------|------|-------------|---------|
| TRIAL | 0 | Evaluation license (limited features/time) | Free |
| STARTER | 1 | Monthly subscription for small teams | $1,999/mo, 3 seats |
| BUSINESS | 2 | One-time payment for growing teams (1 year) | $19,999 + optional $3,999/mo, 10 seats |
| ENTERPRISE | 3 | One-time payment for large organizations (1 year) | $49,999 + optional $9,999/mo, 50 seats |
| DEVELOPER | 4 | Building apps with Owl Browser | Custom |
| SUBSCRIPTION | 5 | Monthly validation required | Custom |

**Note:** All license types now require server activation for seat tracking. Subscription licenses additionally require periodic validation checks.

## Seat Management

### How It Works

1. When a license is activated on a device, the browser sends the hardware fingerprint to the server
2. The server checks if this fingerprint already has an active seat
3. If not, and `seat_count < max_seats`, a new seat is created
4. If `seat_count >= max_seats`, activation is rejected
5. Admins can deactivate seats to free them up for other devices

### Admin UI Features

- **Dashboard**: Shows total active seats and unique devices
- **License Detail**: Shows seat usage (X/Y) and allows deactivation
- **Active Seats Page**: Lists all active seats across all licenses
- **Activations Page**: Shows recent activation attempts with success/failure status

## Webhook Integration

For payment provider integration (Stripe, PayPal, etc.), use the webhook endpoints:

### Cancel Subscription
```bash
curl -X POST http://localhost:3034/api/v1/admin/subscription/cancel \
  -H "Content-Type: application/json" \
  -d '{
    "license_id": "uuid-here",
    "api_key": "your-webhook-key",
    "reason": "Payment failed"
  }'
```

### Reactivate Subscription
```bash
curl -X POST http://localhost:3034/api/v1/admin/subscription/reactivate \
  -H "Content-Type: application/json" \
  -d '{
    "license_id": "uuid-here",
    "api_key": "your-webhook-key"
  }'
```

## Customer Portal

The customer portal allows end-users to manage their licenses without admin intervention.

### Features

- **Registration** - Customers register using their License ID + email (must match license)
- **Dashboard** - View license status, seat usage, and subscription info
- **License Management** - View all licenses, deactivate seats
- **Profile** - Update email, change password
- **Billing** - View invoice history

### Access

- URL: `http://your-server:3035/portal`
- Registration requires a valid License ID and matching email
- One user account per license (enforced by unique constraint)

### Customer Registration Flow

1. Admin creates license with customer email in admin panel
2. Customer visits `/portal/register`
3. Customer enters License ID + email
4. System validates license exists and email matches
5. Customer creates password
6. Customer can now log in and manage their license

## Key Management

### Understanding the Key Pair

The RSA key pair is critical for license security:

| Key | Location | Purpose |
|-----|----------|---------|
| **Private Key** | Server only (`keys/private_key.pem`) | Signs API responses, generates licenses |
| **Public Key** | Embedded in browser binary | Verifies signatures, decrypts licenses |

### Key Synchronization

**The same key pair must be used everywhere:**

1. **Browser Build** - Public key embedded in `src/owl_license.cc`
2. **License Server** - Both keys in `keys/` or `~/.owl_license/`
3. **License Generator** - Both keys in `~/.owl_license/`

### Generating New Keys

```bash
# Using the license generator (macOS/Linux)
./build/license_generator keygen

# Or using OpenSSL directly
openssl genpkey -algorithm RSA -out ~/.owl_license/owl_license.key -pkeyopt rsa_keygen_bits:2048
openssl rsa -in ~/.owl_license/owl_license.key -pubout -out ~/.owl_license/owl_license.pub
```

### Rotating Keys (CAUTION!)

Rotating keys will **invalidate ALL existing licenses**. Only do this if keys are compromised.

```bash
# 1. Generate new keys
./scripts/manage_license_keys.sh rotate

# 2. Update embedded key in browser
./scripts/update_embedded_key.sh

# 3. Rebuild browser
npm run build

# 4. Distribute new browser to all users

# 5. Issue new licenses to all customers
```

## Security Considerations

1. **Change default credentials** - Never use `admin/admin` in production
2. **Generate secure keys** - Use strong random values for all secret keys
3. **Use HTTPS** - Put the servers behind a reverse proxy with SSL
4. **Restrict access** - Admin server should only be accessible internally
5. **Database security** - Use strong passwords for PostgreSQL/MySQL
6. **Backup regularly** - Especially the database and RSA keys
7. **Keep HMAC secret safe** - If compromised, regenerate and rebuild browser
8. **RSA key protection** - Private key should only exist on the server
9. **Key pair backup** - Store encrypted backup of keys in secure location

## Production Deployment

### Docker Deployment (Recommended)

Docker provides a complete, self-contained deployment with PostgreSQL 17:

```bash
cd license-server

# Prepare keys and configuration
mkdir -p keys
cp ~/.owl_license/owl_license.key keys/private_key.pem
cp ~/.owl_license/owl_license.pub keys/public_key.pem
cp .env.example .env
# Edit .env with production values

# Build and deploy
./build-docker.sh
docker-compose up -d
```

#### Docker Architecture

The Docker image includes:
- **PostgreSQL 17** - Production-grade database
- **Admin Server** - Web UI on port 3035
- **Customer Portal** - Self-service at `/portal`
- **Subscription Server** - API on port 3034
- **License Generator** - Compiled for Linux (generates .olic files)
- **Supervisor** - Process management

#### Docker Volumes

| Volume | Purpose |
|--------|---------|
| `./keys:/app/keys` | RSA key pair (read-only) |
| `./data:/app/data` | Application data |
| `./licenses:/app/licenses` | Generated license files |
| `./logs:/app/logs` | Application logs |
| `owl-db-data` | PostgreSQL data (named volume) |

#### Generate License in Docker

```bash
# Enter the container
docker exec -it owl-license-server bash

# Generate a license
/app/license_generator generate \
  --name "John Doe" \
  --email "john@example.com" \
  --type subscription \
  --seats 3 \
  --expiry 365 \
  --output /app/licenses/john_doe.olic

# View license info
/app/license_generator info /app/licenses/john_doe.olic
```

#### Docker Health Checks

```bash
# Check container health
docker-compose ps

# Check service logs
docker-compose logs -f admin-server
docker-compose logs -f subscription-server

# Manual health check
curl http://localhost:3034/api/v1/health
```

### Using Gunicorn (Without Docker)

```bash
pip install gunicorn

# Subscription server (public-facing)
gunicorn -w 4 -b 0.0.0.0:3034 subscription_server:app

# Admin server (internal only)
gunicorn -w 2 -b 127.0.0.1:3035 admin_server:app
```

### Using Nginx as Reverse Proxy

```nginx
server {
    listen 443 ssl;
    server_name license.yourdomain.com;

    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;

    # License API (public)
    location /api/ {
        proxy_pass http://127.0.0.1:3034;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # Admin panel (restrict access)
    location /admin/ {
        # Restrict to internal IPs
        allow 10.0.0.0/8;
        allow 192.168.0.0/16;
        deny all;

        proxy_pass http://127.0.0.1:3035/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    # Customer portal (public)
    location /portal/ {
        proxy_pass http://127.0.0.1:3035/portal/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

## AWS Deployment (ECR + EC2)

Deploy the license server to AWS using ECR for container registry and EC2 for hosting.

### Prerequisites

- AWS CLI installed and configured
- AWS account with ECR and EC2 permissions
- EC2 instance with Docker installed (or use the setup script)
- IAM role attached to EC2 with ECR pull permissions

### Step 1: Push to ECR

```bash
# Push Docker image to ECR (creates repository if needed)
./deploy/ecr-push.sh

# With custom options
./deploy/ecr-push.sh --region us-west-2 --tag v1.0.0
```

The script will:
1. Create ECR repository if it doesn't exist
2. Authenticate Docker to ECR
3. Build the Docker image
4. Tag and push to ECR

### Step 2: Deploy to EC2

**Option A: Deploy to existing EC2 instance**

```bash
# Deploy using SSH
./deploy/ec2-deploy.sh --host ec2-xx-xx-xx-xx.compute-1.amazonaws.com --key ~/.ssh/your-key.pem

# With custom image
./deploy/ec2-deploy.sh --host 1.2.3.4 --key ~/.ssh/key.pem --image 123456789.dkr.ecr.us-east-1.amazonaws.com/owl-license-server:latest
```

**Option B: Set up new EC2 instance**

Use the EC2 User Data script when launching a new instance:

```bash
# Copy contents of deploy/ec2-userdata.sh into EC2 User Data field
# Or run manually on a fresh instance:
sudo bash deploy/ec2-userdata.sh
```

This script:
- Installs Docker
- Creates application directories
- Sets up systemd service
- Creates helper scripts for updates

### Step 3: Configure the Server

After deployment, SSH into the EC2 instance:

```bash
ssh -i ~/.ssh/your-key.pem ec2-user@your-ec2-ip

# Copy your RSA keys
cd /opt/owl-license-server  # or ~/owl-license-server
scp -i ~/.ssh/your-key.pem keys/private_key.pem ec2-user@your-ec2-ip:~/owl-license-server/keys/
scp -i ~/.ssh/your-key.pem keys/public_key.pem ec2-user@your-ec2-ip:~/owl-license-server/keys/

# Edit secrets
nano secrets.env

# Start the service
sudo systemctl start owl-license-server
```

### Manual Docker Run (Quick Reference)

If deploying manually without docker-compose or systemd:

```bash
# Create secrets.env with sensitive values
cat > /home/ec2-user/owl-license-server/secrets.env << EOF
OLIB_DB_PASSWORD=your_secure_db_password
OLIB_ADMIN_PASSWORD_HASH=your_password_hash
OWL_NONCE_HMAC_SECRET=your_hmac_secret_matching_browser_build
EOF

# Run the container with source code mounts for easy updates
# Note: Mount individual .py files (not the whole src/ dir) to avoid read-only conflicts
docker run -d \
    --name owl-license-server \
    --restart unless-stopped \
    -p 3034:3034 \
    -p 3035:3035 \
    --env-file /home/ec2-user/owl-license-server/secrets.env \
    -v owl-db-data:/var/lib/postgresql/17/main \
    -v /home/ec2-user/owl-license-server/keys:/app/keys:ro \
    -v /home/ec2-user/owl-license-server/data:/app/data \
    -v /home/ec2-user/owl-license-server/licenses:/app/licenses \
    -v /home/ec2-user/owl-license-server/src/config.py:/app/config.py:ro \
    -v /home/ec2-user/owl-license-server/src/subscription_server.py:/app/subscription_server.py:ro \
    -v /home/ec2-user/owl-license-server/src/admin_server.py:/app/admin_server.py:ro \
    -v /home/ec2-user/owl-license-server/src/customer_portal.py:/app/customer_portal.py:ro \
    -v /home/ec2-user/owl-license-server/src/database.py:/app/database.py:ro \
    -v /home/ec2-user/owl-license-server/src/init_database.py:/app/init_database.py:ro \
    -v /home/ec2-user/owl-license-server/src/email_service.py:/app/email_service.py:ro \
    -v /home/ec2-user/owl-license-server/src/scheduler_service.py:/app/scheduler_service.py:ro \
    -v /home/ec2-user/owl-license-server/static:/app/static:ro \
    -v /home/ec2-user/owl-license-server/templates:/app/templates:ro \
    YOUR_ECR_IMAGE:latest
```

**Critical:** The `OWL_NONCE_HMAC_SECRET` must match the value compiled into the browser (from `.env` at build time). If they don't match, you'll get "HMAC verification failed" errors.

### Updating Python Files (Without Rebuild)

To deploy code changes without rebuilding the Docker image:

```bash
# Copy updated Python files to EC2
scp -i ~/.ssh/your-key.pem license-server/src/*.py ec2-user@your-ec2-ip:~/owl-license-server/src/

# Restart the container to reload Python
ssh -i ~/.ssh/your-key.pem ec2-user@your-ec2-ip "docker restart owl-license-server"
```

### Step 4: Update Deployment

To deploy a new version:

```bash
# On your local machine
./deploy/ecr-push.sh

# On the EC2 instance
cd /opt/owl-license-server
./update.sh
```

### Security Groups

Ensure your EC2 security group allows:

| Port | Protocol | Source | Purpose |
|------|----------|--------|---------|
| 22 | TCP | Your IP | SSH access |
| 3034 | TCP | 0.0.0.0/0 | License API |
| 3035 | TCP | Your IP / VPN | Admin Panel |

**Recommendation:** Restrict port 3035 (admin panel) to your IP or VPN only.

### IAM Role for EC2

Attach an IAM role to your EC2 instance with this policy:

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
                "ecr:BatchGetImage"
            ],
            "Resource": "*"
        }
    ]
}
```

---

## Development

### Running in Development Mode

Both servers run with `debug=True` by default, providing:
- Auto-reload on code changes
- Detailed error messages
- Request logging

### Docker Development Mode (Live Code Updates)

For development with Docker, use the development compose override to mount source code:

```bash
# Start with development mode (mounts source code)
docker-compose -f docker-compose.yml -f docker-compose.dev.yml up -d

# Make code changes to .py files or templates...

# Reload servers to apply changes (no rebuild needed!)
./dev-reload.sh           # Reload all servers
./dev-reload.sh admin     # Reload admin server only
./dev-reload.sh api       # Reload API server only

# Check server status
./dev-reload.sh status

# View logs
./dev-reload.sh logs
```

This mounts the following directories into the container:
- `*.py` - Python source files
- `templates/` - Jinja2 templates
- `static/` - CSS, JS, images

**Note:** Changes to `requirements.txt` or `Dockerfile` still require a rebuild.

### Testing the Activation Flow

1. Create a license in the admin UI
2. Note the License ID
3. Test the activation:

```bash
curl -X POST http://localhost:3034/api/v1/license/activate \
  -H "Content-Type: application/json" \
  -d '{
    "license_id": "your-license-id",
    "hardware_fingerprint": "test-fingerprint-abc123"
  }'
```

4. Try activating again with a different fingerprint to test seat limits:

```bash
curl -X POST http://localhost:3034/api/v1/license/activate \
  -H "Content-Type: application/json" \
  -d '{
    "license_id": "your-license-id",
    "hardware_fingerprint": "different-fingerprint-xyz789"
  }'
```

## License

Part of the Owl Browser project.
