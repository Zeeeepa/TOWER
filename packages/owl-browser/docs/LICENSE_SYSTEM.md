# Owl Browser Licensing System

This document describes the licensing system for Owl Browser, including how to generate licenses, distribute them to customers, and how end-users can activate their licenses.

## Overview

Owl Browser uses a cryptographically secure licensing system with server-side activation tracking for all license types.

### Security Features

- **RSA-2048 digital signatures** - Only authorized personnel can create valid licenses
- **AES-256 encryption** - License data is encrypted and tamper-proof
- **HMAC-SHA256 authentication** - Detects any modification to license files
- **Server activation** - All license types require server activation for seat tracking
- **RSA-signed server responses** - Prevents MITM attacks on server communication
- **HMAC-authenticated requests** - Mutual authentication between browser and server
- **Hardware binding** (optional) - Licenses can be bound to specific machines

### Universal Activation (All License Types)

All license types now require server activation when first installed. This enables:
- **Seat tracking** - Track active devices per license by hardware fingerprint
- **Max seats enforcement** - Prevent licenses from being used on more devices than allowed
- **Activation audit logs** - Record all activation attempts for compliance

### Subscription Licenses

Subscription licenses have additional features:
- **Monthly server validation** - Periodic check to ensure subscription is active
- **Grace period** - Offline use allowed during server unreachability (default: 7 days)
- **Reactivation support** - Canceled subscriptions can be reactivated
- **Secure state storage** - Hardware-bound encrypted local state prevents tampering

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        LICENSE GENERATION                           │
│                     (Your secure machine only)                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   Private Key (RSA-2048)          License Generator CLI             │
│   ~/.owl_license/owl_license.key  →  Signs license data           │
│                                        Encrypts with AES-256        │
│                                        Outputs .olic file           │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Distribute .olic file
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        LICENSE ACTIVATION                           │
│                       (Customer's machine)                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   Owl Browser                         License Server               │
│   ┌──────────────┐                     ┌──────────────┐             │
│   │ 1. Verify    │                     │              │             │
│   │    signature │                     │  Database:   │             │
│   │ 2. Decrypt   │ ─── Activate ────▶  │  - Licenses  │             │
│   │    license   │     (HTTPS+HMAC)    │  - Seats     │             │
│   │ 3. Check     │ ◀── RSA-Signed ──── │  - Logs      │             │
│   │    response  │     Response        │              │             │
│   └──────────────┘                     └──────────────┘             │
│                                                                     │
│   Embedded: Public Key, HMAC Secret    Private Key (RSA signing)    │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Communication Security

1. **Browser → Server**: HMAC-authenticated nonce prevents replay attacks
2. **Server → Browser**: RSA-signed responses prevent MITM attacks
3. **All communication**: Over HTTPS in production

## License Types

| Type | Value | Description |
|------|-------|-------------|
| `trial` | 0 | Time-limited trial license |
| `personal` | 1 | Single-user personal license |
| `professional` | 2 | Professional license with more features |
| `enterprise` | 3 | Enterprise license with unlimited seats |
| `developer` | 4 | Developer license for testing/development |
| `subscription` | 5 | Subscription-based license with monthly server validation |

## License File Format

License files use the `.olic` extension (Olib License) and contain:

- Magic number: `0x4F4C4943` ("OLIC")
- Version number
- Encrypted license data (AES-256-CBC + HMAC-SHA256)
- RSA-2048 digital signature
- Checksum

---

# For License Administrators

## Initial Setup (One-Time)

### 1. Build the License Generator

```bash
cd /path/to/owl-browser
bash scripts/build_license_generator.sh
```

This creates the license generator at `build/license/license_generator`.

### 2. Generate RSA Key Pair

```bash
./build/license/license_generator keygen
```

Output:
```
Keys generated successfully!
Private key: /Users/you/.owl_license/owl_license.key
Public key: /Users/you/.owl_license/owl_license.pub

IMPORTANT: Keep the private key secure!
The public key should be embedded in the browser source.
```

**Security Notes:**
- The **private key** (`owl_license.key`) must be kept secure and never shared
- Back up the private key securely - if lost, you cannot generate new licenses
- The **public key** (`owl_license.pub`) is embedded in the browser binary during compilation

### 3. Key Management

Use npm scripts or shell scripts directly to manage license keys:

#### NPM Scripts (Recommended)

```bash
# Check current key status
npm run license:status

# Check if embedded key matches current public key
npm run license:check-key

# Backup keys (creates encrypted archive)
npm run license:backup

# Update embedded key in source (fully automated)
npm run license:update-key

# Build the license generator
npm run license:build-generator
```

#### Shell Scripts (Direct Access)

```bash
# Check current key status
./scripts/manage_license_keys.sh status

# Backup keys (creates encrypted archive)
./scripts/manage_license_keys.sh backup

# Restore keys from backup
./scripts/manage_license_keys.sh restore backup_file.tar.gz.enc

# Rotate keys (WARNING: invalidates ALL existing licenses!)
./scripts/manage_license_keys.sh rotate
```

**Key Backup Best Practices:**
- Create encrypted backups immediately after key generation
- Store backups in multiple secure locations (password manager, encrypted cloud, physical safe)
- Never commit key backups to git
- Test backup restoration periodically

### 4. Embed Public Key in Browser (Automated)

When you generate a new key pair, rotate keys, or set up on a new machine, update the embedded key with a single command:

```bash
# Automatically reads ~/.owl_license/owl_license.pub
# Generates XOR-obfuscated bytes
# Updates src/owl_license.cc
npm run license:update-key

# Then rebuild the browser
npm run build
```

To verify the embedded key matches your current public key without updating:

```bash
npm run license:check-key
```

**Note:** The public key is XOR-obfuscated to prevent extraction via `strings`. The automation handles this transparently.

## Generating Licenses

### Basic License Generation

```bash
./build/license/license_generator generate \
  --name "John Doe" \
  --email "john@example.com" \
  --type professional \
  --output john_license.olic
```

### Full Options

```bash
./build/license/license_generator generate \
  --name "John Doe" \
  --org "Acme Corporation" \
  --email "john@acme.com" \
  --type enterprise \
  --seats 50 \
  --expiry 365 \
  --notes "Annual enterprise license" \
  --output acme_license.olic
```

### Command Reference

| Option | Required | Description |
|--------|----------|-------------|
| `--name <name>` | Yes | Licensee name |
| `--email <email>` | Yes | Contact email |
| `--output <file>` | Yes | Output license file path |
| `--org <organization>` | No | Organization name |
| `--type <type>` | No | License type (default: `trial`) |
| `--seats <n>` | No | Number of allowed seats/devices (default: 1) |
| `--expiry <days>` | No | Days until expiry, 0 = perpetual (default: 0) |
| `--hardware-bound` | No | Bind license to specific hardware |
| `--grace-period <days>` | No | Days for offline use (subscription only, default: 7) |
| `--notes <text>` | No | Internal notes (not visible to user) |

### Examples

**30-day Trial License:**
```bash
./build/license/license_generator generate \
  --name "Trial User" \
  --email "trial@example.com" \
  --type trial \
  --expiry 30 \
  --output trial.olic
```

**Perpetual Personal License:**
```bash
./build/license/license_generator generate \
  --name "Jane Smith" \
  --email "jane@gmail.com" \
  --type personal \
  --seats 1 \
  --output jane_personal.olic
```

**1-Year Enterprise License (50 seats):**
```bash
./build/license/license_generator generate \
  --name "IT Department" \
  --org "Big Corp Inc." \
  --email "it@bigcorp.com" \
  --type enterprise \
  --seats 50 \
  --expiry 365 \
  --output bigcorp_enterprise.olic
```

**1-Year Subscription License:**
```bash
./build/license/license_generator generate \
  --name "Monthly Subscriber" \
  --org "Startup Inc." \
  --email "billing@startup.com" \
  --type subscription \
  --expiry 365 \
  --grace-period 7 \
  --output subscription_license.olic
```

**Important:** After generating a subscription license, you **MUST** register the `license_id` in the license server database. The license will not work until it is registered as active.

## Verifying Licenses

Check if a license file is valid:

```bash
./build/license/license_generator verify customer_license.olic
```

Output:
```
License verified successfully!
{
  "license_id": "19df0c5e-3114-4aa2-843e-cd2790557e66",
  "name": "John Doe",
  "organization": "Acme Corporation",
  "email": "john@acme.com",
  "type": 2,
  "type_name": "professional",
  "max_seats": 10,
  "issue_timestamp": 1764205623,
  "expiry_timestamp": 1795741623,
  "feature_flags": 0,
  "hardware_bound": false,
  "issuer": "Olib License Generator",
  "notes": ""
}
```

## Viewing License Information

```bash
./build/license/license_generator info customer_license.olic
```

---

# Subscription License System

Subscription licenses require a license server to validate that the subscription is still active. This section covers the architecture, server setup, and database requirements.

## Subscription Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SUBSCRIPTION FLOW                                   │
└─────────────────────────────────────────────────────────────────────────────┘

1. LICENSE GENERATION (Admin)
   ┌────────────────────┐
   │ License Generator  │ ──▶ Generates .olic file with type=subscription
   └────────────────────┘     License ID: e.g., "a1b2c3d4-..."
            │
            ▼
   ┌────────────────────┐
   │  License Server    │ ◀── Admin registers license_id in database
   │     Database       │     Sets subscription_status = 'active'
   └────────────────────┘

2. FIRST ACTIVATION (End User)
   ┌────────────────────┐      ┌────────────────────┐
   │   Owl Browser     │ ───▶ │   License Server   │
   │  (Customer PC)     │      │   (Your Server)    │
   └────────────────────┘      └────────────────────┘
            │                           │
            │  POST /api/v1/subscription/check
            │  { license_id, hardware_fingerprint, timestamp }
            │                           │
            │◀──────────────────────────│
            │  { active: true, signature: "..." }
            │                           │
            ▼
   ┌────────────────────┐
   │ Secure Local State │ ◀── Stores activation time, next check date
   │ (Encrypted file)   │     Hardware-bound encryption
   └────────────────────┘

3. MONTHLY CHECKS (Automatic)
   - If activated Jan 3rd 11:00 AM, next check is Feb 3rd 11:00 AM
   - Browser checks server on exact monthly anniversary
   - If server unreachable: grace period applies (default 7 days)
   - If subscription canceled: license becomes invalid

4. REACTIVATION (After Cancellation)
   - When subscription is inactive, browser checks server on EVERY launch
   - If subscription reactivated in database, browser resumes normal operation
```

## License Server Setup

The license server is located in the `license-server/` directory and includes:
- **Admin Server** (port 3035) - Web UI for license management
- **Subscription Server** (port 3034) - REST API for browser validation

### Prerequisites

```bash
cd license-server
pip install -r requirements.txt
```

### Starting the Servers

```bash
# Start admin server (web UI) on localhost:3035
python3 admin_server.py

# Start subscription server on localhost:3034
python3 subscription_server.py
```

### Admin Web Interface

The admin server provides a beautiful web UI for:
- Creating and managing licenses
- Viewing subscription status
- Downloading license files
- Audit logging

Access at: http://127.0.0.1:3035

Default credentials:
- Username: `admin`
- Password: `admin` (change in production!)

### Server Configuration

The license server URL is configured in `.env`:

```env
# Development (localhost)
OLIB_LICENSE_SERVER_URL=http://localhost:3034

# Production (replace with your domain)
OLIB_LICENSE_SERVER_URL=https://license.owlbrowser.net
```

CMake reads this value and compiles it into the browser binary.

### Production Deployment

For production, the license server should be:

1. **Hosted on HTTPS** with a valid SSL certificate
2. **Behind a load balancer** for high availability
3. **Connected to a production database** (PostgreSQL recommended)
4. **Protected by rate limiting** to prevent abuse

Update `.env` for production:
```env
OLIB_DB_TYPE=postgresql
OLIB_DB_HOST=your-db-host
OLIB_DB_PORT=5432
OLIB_DB_NAME=owl_licenses
OLIB_DB_USER=olib
OLIB_DB_PASSWORD=your-secure-password

OLIB_ADMIN_PASSWORD_HASH=pbkdf2:sha256:...
OLIB_ADMIN_SECRET_KEY=your-random-secret-key
```

Run with a production WSGI server:
```bash
# Using gunicorn
pip install gunicorn
gunicorn -w 4 -b 0.0.0.0:3034 subscription_server:app
gunicorn -w 2 -b 127.0.0.1:3035 admin_server:app
```

## License Server API

### Universal License Activation (All License Types)

**All license types** must call this endpoint when first activated. This enables seat tracking, max seats enforcement, and audit logging.

```
POST /api/v1/license/activate
Content-Type: application/json
X-Nonce: <random_hex>.<timestamp>.<hmac_hex>

{
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "hardware_fingerprint": "sha256-hash-of-hardware"
}
```

**Response (Success):**
```json
{
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "activated": true,
    "seat_count": 1,
    "max_seats": 3,
    "license_type": 2,
    "license_type_name": "PROFESSIONAL",
    "activation_timestamp": 1764364494,
    "server_timestamp": 1764364494,
    "signature": "base64-rsa-signature"
}
```

**Response (Max Seats Exceeded):**
```json
{
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "activated": false,
    "seat_count": 3,
    "max_seats": 3,
    "error": "Maximum seats (3) reached",
    "license_type": 2,
    "license_type_name": "PROFESSIONAL",
    "server_timestamp": 1764364494,
    "signature": "base64-rsa-signature"
}
```

### HMAC-Authenticated Nonce Format

The `X-Nonce` header provides replay protection and mutual authentication:

```
X-Nonce: <random_hex>.<timestamp>.<hmac_hex>
```

- `random_hex`: 32 characters (16 bytes hex-encoded)
- `timestamp`: Unix timestamp in decimal
- `hmac_hex`: HMAC-SHA256 of `random_hex + timestamp + license_id` using shared secret

The server verifies:
1. HMAC matches expected value (proves browser has the shared secret)
2. Timestamp is within 5-minute tolerance (prevents replay attacks)

### RSA-Signed Responses

All server responses include a `signature` field containing a Base64-encoded RSA-PKCS1v15-SHA256 signature of the response JSON (excluding the signature field). The browser verifies this using the embedded public key to prevent MITM attacks.

### Check Subscription Status

For subscription licenses, periodic checks verify the subscription is still active:

```
POST /api/v1/subscription/check
Content-Type: application/json
X-Nonce: <random_hex>.<timestamp>.<hmac_hex>

{
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "hardware_fingerprint": "sha256-hash-of-hardware"
}
```

**Response (Active):**
```json
{
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "active": true,
    "server_timestamp": 1704067200,
    "next_check_timestamp": 1706745600,
    "signature": "base64-rsa-signature"
}
```

**Response (Inactive):**
```json
{
    "license_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "active": false,
    "reason": "subscription_canceled",
    "server_timestamp": 1704067200,
    "signature": "base64-rsa-signature"
}
```

### Reason Codes

| Reason | Description |
|--------|-------------|
| `active` | Subscription is active and valid |
| `grace_period` | Subscription canceled but within grace period |
| `subscription_expired` | Subscription end date has passed |
| `subscription_canceled` | Subscription was canceled |
| `subscription_suspended` | Subscription temporarily suspended |
| `license_not_found` | License ID not in database |
| `Maximum seats (N) reached` | All allowed seats are in use |

### Admin Web Interface

The admin server provides a beautiful dark-themed web UI at http://127.0.0.1:3035:

| Page | Description |
|------|-------------|
| Dashboard | Overview statistics, recent licenses, activity |
| Licenses | List all licenses with search and filtering |
| License Detail | View/edit license, manage seats, see activation history |
| Subscriptions | Manage subscription-specific settings |
| Activations | Recent activation attempts (success/failure) |
| Active Seats | All active seats across all licenses |
| Audit Log | Admin action history |

### Webhook Endpoints (for Payment Providers)

**Cancel Subscription:**
```bash
POST /api/v1/admin/subscription/cancel
Content-Type: application/json

{
    "license_id": "uuid-here",
    "api_key": "your-webhook-key",
    "reason": "Payment failed"
}
```

**Reactivate Subscription:**
```bash
POST /api/v1/admin/subscription/reactivate
Content-Type: application/json

{
    "license_id": "uuid-here",
    "api_key": "your-webhook-key"
}
```

## Database Schema

The license server uses the following database schema. For production, replace SQLite with PostgreSQL or MySQL.

### Licenses Table

```sql
CREATE TABLE licenses (
    id TEXT PRIMARY KEY,                        -- UUID
    name TEXT NOT NULL,                         -- Licensee name
    email TEXT NOT NULL,
    organization TEXT,
    license_type INTEGER NOT NULL,              -- 0=Trial, 1=Personal, 2=Pro, etc.
    max_seats INTEGER DEFAULT 1,                -- Max concurrent devices
    issue_date TEXT NOT NULL,
    expiry_date TEXT,                           -- NULL = perpetual
    hardware_bound INTEGER DEFAULT 0,
    hardware_fingerprint TEXT,
    feature_flags INTEGER DEFAULT 0,
    status TEXT DEFAULT 'active',               -- active, revoked, expired
    notes TEXT,
    issuer TEXT,
    file_path TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

### Subscriptions Table

```sql
CREATE TABLE subscriptions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    license_id TEXT UNIQUE NOT NULL,
    status TEXT DEFAULT 'active',               -- active, canceled, suspended
    activation_date TEXT,
    last_check_date TEXT,
    next_check_date TEXT,
    grace_period_days INTEGER DEFAULT 7,
    canceled_at TEXT,
    FOREIGN KEY (license_id) REFERENCES licenses(id)
);
```

### License Seats Table (Seat Tracking)

```sql
CREATE TABLE license_seats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    license_id TEXT NOT NULL,
    hardware_fingerprint TEXT NOT NULL,
    first_activated_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    ip_address TEXT,
    user_agent TEXT,
    is_active INTEGER DEFAULT 1,
    deactivated_at TEXT,
    UNIQUE(license_id, hardware_fingerprint),
    FOREIGN KEY (license_id) REFERENCES licenses(id)
);
```

### Activation Logs Table

```sql
CREATE TABLE activation_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    license_id TEXT NOT NULL,
    action TEXT NOT NULL,                       -- activation, subscription_check, etc.
    hardware_fingerprint TEXT,
    ip_address TEXT,
    user_agent TEXT,
    success INTEGER DEFAULT 1,
    error_message TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (license_id) REFERENCES licenses(id)
);
```

### Admin Audit Log Table

```sql
CREATE TABLE admin_audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    admin_user TEXT NOT NULL,
    action TEXT NOT NULL,
    target_type TEXT,
    target_id TEXT,
    details TEXT,
    ip_address TEXT,
    created_at TEXT NOT NULL
);
```

## Production Database Integration

For production, you need to integrate the license server with your billing/subscription system. Here's what needs to be implemented:

### 1. Payment Provider Webhook Integration

When using Stripe, Paddle, or similar:

```python
# Example: Stripe webhook handler (to be implemented)
@app.route('/webhooks/stripe', methods=['POST'])
def stripe_webhook():
    event = stripe.Webhook.construct_event(
        request.data,
        request.headers['Stripe-Signature'],
        STRIPE_WEBHOOK_SECRET
    )

    if event['type'] == 'customer.subscription.created':
        # Create subscription in database
        license_id = generate_license_for_customer(event['data']['object'])
        db.create_subscription(license_id, ...)

    elif event['type'] == 'customer.subscription.deleted':
        # Mark subscription as canceled
        license_id = get_license_for_stripe_subscription(event['data']['object']['id'])
        db.update_subscription_status(license_id, 'canceled')

    elif event['type'] == 'invoice.payment_succeeded':
        # Update last payment date
        license_id = get_license_for_stripe_subscription(event['data']['object']['subscription'])
        update_payment_date(license_id)

    return '', 200
```

### 2. Database Adapter for Production

Replace SQLite with your production database:

```python
# PostgreSQL adapter example (to be implemented)
class PostgresSubscriptionDatabase(SubscriptionDatabase):
    def __init__(self, connection_string: str):
        self.pool = create_connection_pool(connection_string)

    def get_subscription(self, license_id: str) -> Optional[Dict]:
        with self.pool.connection() as conn:
            cursor = conn.cursor()
            cursor.execute(
                "SELECT * FROM subscriptions WHERE license_id = %s",
                (license_id,)
            )
            return cursor.fetchone()
```

### 3. Admin Dashboard Integration

Connect to your existing admin panel:

```python
# Example: Fetch subscription data for admin dashboard
def get_subscription_metrics():
    return {
        'total_active': db.count_by_status('active'),
        'total_canceled': db.count_by_status('canceled'),
        'revenue_this_month': calculate_mrr(),
        'churn_rate': calculate_churn(),
    }
```

### 4. Email Notifications

Send notifications for subscription events:

```python
def notify_subscription_expiring(license_id: str, days_remaining: int):
    subscription = db.get_subscription(license_id)
    send_email(
        to=subscription['customer_email'],
        subject=f"Your Owl Browser subscription expires in {days_remaining} days",
        template='subscription_expiring',
        data=subscription
    )
```

## Secure State Storage

The browser stores subscription state locally to:
1. Track activation timestamp
2. Remember last successful check
3. Calculate next check date
4. Enable grace period during server outages

### State File Location

- **macOS:** `~/Library/Application Support/OlibBrowser/.subscription_state`
- **Linux:** `~/.config/owl-browser/.subscription_state`
- **Windows:** `%APPDATA%\OlibBrowser\.subscription_state`

### Security Features

The state file is protected by:

1. **Hardware-bound encryption** - AES-256 key derived from hardware fingerprint + salt
2. **Integrity hash** - SHA-256 hash of all state fields
3. **Tamper detection** - State rejected if integrity check fails
4. **Cannot be copied** - Encrypted with machine-specific key

If someone tries to:
- **Edit the file** → Integrity hash fails → State rejected
- **Copy to another machine** → Decryption fails → State rejected
- **Hex edit values** → AES-GCM authentication fails → State rejected

## Subscription Workflow Summary

### For License Administrators

1. **Generate subscription license:**
   ```bash
   ./build/license/license_generator generate \
     --name "Customer Name" \
     --email "customer@example.com" \
     --type subscription \
     --expiry 365 \
     --grace-period 7 \
     --output customer_license.olic
   ```

2. **Register in database:**
   ```bash
   curl -X POST http://localhost:3034/admin/subscriptions \
     -H "Content-Type: application/json" \
     -d '{
       "license_id": "<license_id_from_step_1>",
       "customer_name": "Customer Name",
       "customer_email": "customer@example.com"
     }'
   ```

3. **Send license file to customer**

4. **Manage subscription:**
   ```bash
   # Cancel subscription
   curl -X PUT http://localhost:3034/admin/subscriptions/<license_id>/status \
     -H "Content-Type: application/json" \
     -d '{"status": "canceled"}'

   # Reactivate subscription
   curl -X PUT http://localhost:3034/admin/subscriptions/<license_id>/status \
     -H "Content-Type: application/json" \
     -d '{"status": "active"}'
   ```

### For End Users

Subscription licenses work the same as regular licenses from the user's perspective:

```bash
# Activate license
owl_browser --license add /path/to/subscription_license.olic

# Check status (shows subscription info)
owl_browser --license status
```

The monthly server checks happen automatically in the background.

---

# For End Users

## Browser Versions

Owl Browser comes in two versions:

| Version | Binary | Description |
|---------|--------|-------------|
| **Headless** | `owl_browser` | Command-line browser for automation (MCP server) |
| **UI** | `owl_browser_ui` | Visual browser with graphical interface |

Both versions share the same license file and CLI commands.

## Activating a License

After receiving your `.olic` license file from Olib:

### Headless Version (owl_browser)

```bash
owl_browser --license add /path/to/your/license.olic
```

Or if running from the build directory:
```bash
./build/Release/owl_browser.app/Contents/MacOS/owl_browser --license add /path/to/license.olic
```

### UI Version (owl_browser_ui)

```bash
owl_browser_ui --license add /path/to/your/license.olic
```

Or if running from the build directory:
```bash
./build/Release/owl_browser_ui.app/Contents/MacOS/owl_browser_ui --license add /path/to/license.olic
```

**Note:** The license is shared between both versions. Activating on one version will work for the other.

### Successful Activation

```
License activated successfully!
{"status":"valid","valid":true,"license_id":"...","name":"Your Name",...}
```

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `not_found` | No license file provided | Provide path to .olic file |
| `corrupted` | Invalid or tampered license file | Contact support for a new license |
| `expired` | License has expired | Renew your license |
| `invalid_signature` | License was not issued by Olib | Contact support |

## UI Version: License Error Dialog

When launching the **UI version** (`owl_browser_ui`) without a valid license, a native macOS dialog will appear:

```
┌─────────────────────────────────────────────────────────┐
│  ⚠️  License Required                                    │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  No license file found.                                 │
│                                                         │
│  Owl Browser requires a valid license to run.          │
│                                                         │
│  To activate a license, run from Terminal:              │
│  owl_browser_ui --license add /path/to/license.olic    │
│                                                         │
│  Hardware Fingerprint:                                  │
│  44d1d03688c0b246...                                    │
│                                                         │
│  Contact support@olib.ai for licensing inquiries.       │
│                                                         │
│              [Copy Fingerprint]  [Quit]                 │
└─────────────────────────────────────────────────────────┘
```

The dialog provides:
- **Error description** - What went wrong (no license, expired, invalid, etc.)
- **Hardware Fingerprint** - For requesting hardware-bound licenses
- **Copy Fingerprint** button - Copies fingerprint to clipboard
- **Quit** button - Exits the application

## Checking License Status

```bash
owl_browser --license status
```

Output:
```
License Status: valid
Licensed to: John Doe (Acme Corporation)
Email: john@acme.com
Type: professional
Seats: 10
Expires: 2027-01-15
```

## Viewing License Details

```bash
owl_browser --license info
```

## Removing a License

```bash
owl_browser --license remove
```

## Getting Hardware Fingerprint

For hardware-bound licenses, you may need to provide your machine's fingerprint:

```bash
owl_browser --license fingerprint
```

Output:
```
Hardware Fingerprint: 44d1d03688c0b246410fb81afbffb4c7bba52d94edfef5a10ca035813199af9d
```

Provide this fingerprint when requesting a hardware-bound license.

---

# Technical Details

## License Storage

Licenses are stored at:
- **macOS:** `~/Library/Application Support/OlibBrowser/license.olic`
- **Linux:** `~/.config/OlibBrowser/license.olic`
- **Windows:** `%APPDATA%\OlibBrowser\license.olic`

## Validation Points

The license is validated at multiple points:
1. **Application startup** - Before CEF initialization
2. **Context creation** - When creating new browser contexts
3. **Periodic checks** - During runtime to detect tampering

## Security Measures

### Cryptographic Security
- **RSA-2048** asymmetric encryption for signatures
- **AES-256-CBC** with **HMAC-SHA256** for authenticated encryption
- Keys derived using SHA-256 hash functions
- **XOR-obfuscated** public key storage (prevents `strings` extraction)

### Binary Hardening
- **Symbol stripping** - All internal function names removed from release builds
- **Obfuscated error messages** - Internal strings use generic codes
- **Control flow obfuscation** - Opaque predicates confuse static analysis

### Runtime Integrity Verification
The browser verifies its own integrity at runtime to detect patching/tampering:

1. **Code segment hashing** - FNV-1a hash of binary code regions, compared against startup baseline
2. **Critical function hashing** - Hashes of key license functions to detect targeted patches
3. **Memory protection check** - Verifies code segments are not writable (detects runtime patching)
4. **Code signature verification** - Periodically validates macOS code signature matches disk binary
5. **Function pointer validation** - Ensures license functions haven't been NOP'd out

If any check fails, the browser returns `LicenseStatus::TAMPERED` and refuses to run.

### Anti-Tampering
- Distributed license checks throughout the codebase
- **Encrypted validation tokens** - License state protected by cryptographic proof
- **5-layer anti-debugging:**
  1. ptrace-based detection
  2. sysctl P_TRACED flag check
  3. Timing-based anomaly detection
  4. Debug environment variable scanning
  5. Code integrity verification
- Clock manipulation detection

### Code Signing (Production)

For production distribution, sign the binary with an Apple Developer certificate:

```bash
# Sign with Developer ID certificate
codesign --force --options runtime \
  --sign "Developer ID Application: Your Name (TEAM_ID)" \
  build/Release/owl_browser.app

# Notarize for distribution
xcrun notarytool submit build/Release/owl_browser.app \
  --apple-id "your@email.com" \
  --team-id "TEAM_ID" \
  --password "app-specific-password" \
  --wait

# Staple the notarization ticket
xcrun stapler staple build/Release/owl_browser.app
```

**Note:** Without proper code signing, the binary is ad-hoc signed and can be modified by attackers.

### Hardware Fingerprinting (Optional)
Hardware fingerprints are computed from:
- Machine UUID / IOPlatformUUID
- CPU information
- Primary MAC address
- Disk serial numbers

## File Structure

```
owl-browser/
├── include/
│   └── owl_license.h          # License data structures & subscription types
├── src/
│   └── owl_license.cc         # License validation, server activation, secure state
├── scripts/
│   ├── license_generator.cpp   # License generator source (supports all types)
│   ├── build_license_generator.sh
│   ├── obfuscate_key.py        # XOR key obfuscation tool
│   ├── manage_license_keys.sh  # Key backup/restore/rotation
│   └── update_embedded_key.sh  # Automated key embedding
├── license-server/
│   ├── admin_server.py         # Admin web interface (port 3035)
│   ├── subscription_server.py  # License activation & validation API (port 3034)
│   ├── database.py             # Database abstraction (SQLite/PostgreSQL/MySQL)
│   ├── config.py               # Centralized configuration
│   ├── requirements.txt        # Python dependencies
│   ├── templates/              # HTML templates (Jinja2)
│   │   ├── base.html           # Dark-themed base layout
│   │   ├── dashboard.html      # Statistics dashboard
│   │   ├── licenses.html       # License list
│   │   ├── license_detail.html # License details + seat management
│   │   ├── activations.html    # Recent activation logs
│   │   ├── seats.html          # Active seats overview
│   │   ├── subscriptions.html  # Subscription management
│   │   └── audit_log.html      # Admin action history
│   ├── data/                   # SQLite database (gitignored)
│   ├── licenses/               # Generated license files (gitignored)
│   └── keys/                   # Fallback RSA keys (gitignored)
├── build/
│   └── license/
│       └── license_generator   # Compiled generator
├── .env                        # Environment config (server URL, HMAC secret, etc.)
└── docs/
    └── LICENSE_SYSTEM.md       # This file

~/.owl_license/                 # Key storage (secure location)
├── owl_license.key            # Private key (server signing - KEEP SECURE!)
└── owl_license.pub            # Public key (embedded in browser)

# Subscription State (per-user, encrypted)
~/Library/Application Support/OlibBrowser/.subscription_state  # macOS
~/.config/owl-browser/.subscription_state                     # Linux
%APPDATA%\OlibBrowser\.subscription_state                      # Windows
```

## NPM Scripts Reference

| Script | Description |
|--------|-------------|
| `npm run license:status` | Check key pair status and embedded key verification |
| `npm run license:check-key` | Verify embedded key matches public key file |
| `npm run license:update-key` | Update embedded key in source code (automated) |
| `npm run license:backup` | Create encrypted backup of key pair |
| `npm run license:build-generator` | Build the license generator tool |

---

# Key Management & Disaster Recovery

## What If I Lose the Private Key?

**If you lose the private key (`~/.owl_license/owl_license.key`) and have no backups:**

1. **All existing licenses become unverifiable** - You cannot prove you issued them
2. **You cannot generate new licenses** - The generator requires the private key
3. **You must rotate to a new key pair** - This invalidates ALL existing licenses

**Recovery Steps:**

1. Generate new key pair:
   ```bash
   ./scripts/manage_license_keys.sh rotate
   ```

2. Update the embedded public key (automated):
   ```bash
   npm run license:update-key
   ```

3. Rebuild and redistribute the browser:
   ```bash
   npm run build
   ```

4. **Reissue ALL customer licenses** - Every customer needs a new license file

## New Machine Setup

When setting up on a new MacBook or development machine:

```bash
# 1. Restore keys from your encrypted backup
./scripts/manage_license_keys.sh restore /path/to/backup.tar.gz.enc

# 2. Verify keys are in place
npm run license:status

# 3. Update embedded key in source (single command)
npm run license:update-key

# 4. Rebuild the browser
npm run build

# 5. Verify license still works
./build/Release/owl_browser.app/Contents/MacOS/owl_browser --license status
```

**Note:** Previously issued licenses will continue to work as long as you restore the same private key.

## Backup Procedures

### Create Encrypted Backup

```bash
npm run license:backup
# Or: ./scripts/manage_license_keys.sh backup

# Enter a strong password when prompted
# Creates: owl_license_backup_YYYYMMDD_HHMMSS.tar.gz.enc
```

### Restore from Backup

```bash
./scripts/manage_license_keys.sh restore /path/to/backup.tar.gz.enc
# Enter the password used during backup
```

### Recommended Backup Strategy

1. **Immediately after key generation:** Create encrypted backup
2. **Store in multiple locations:**
   - Password manager (e.g., 1Password, Bitwarden)
   - Encrypted cloud storage (e.g., encrypted S3 bucket)
   - Physical secure storage (e.g., safe deposit box)
3. **Test restoration quarterly:** Verify backups are readable
4. **Document the backup password:** Store separately from the backup itself

## Key Rotation

Key rotation should be performed if:
- Private key may have been compromised
- Moving to a new security infrastructure
- Periodic security policy requires it

**Warning:** Key rotation invalidates ALL existing licenses. Plan accordingly.

```bash
# 1. Rotate keys (creates backup automatically)
./scripts/manage_license_keys.sh rotate

# 2. Update embedded key (automated)
npm run license:update-key

# 3. Rebuild the browser
npm run build

# 4. Reissue all customer licenses
```

---

# Troubleshooting

## "License validation failed: not_found"

The browser cannot find a license file. Install one with:
```bash
owl_browser --license add /path/to/license.olic
```

## "License validation failed: corrupted"

The license file is invalid. This can happen if:
- The file was modified or corrupted during transfer
- The browser was compiled with a different public key
- The license was generated with a different private key

Solution: Request a new license file from Olib.

## "License validation failed: expired"

Your license has expired. Contact Olib to renew.

## "License validation failed: hardware_mismatch"

Your hardware-bound license is being used on a different machine than it was issued for. Contact Olib with your new hardware fingerprint.

## Browser Won't Start

If the browser refuses to start, check:
1. Is a license installed? Run `owl_browser --license status`
2. Is the license valid? Check expiry date
3. Is the license for this version? Older licenses may not work with newer versions

## "License validation failed: debug_detected"

The browser detected a debugger attached to the process. This is a security measure to prevent reverse engineering.

**Causes:**
- Running under LLDB, GDB, or other debuggers
- Debug environment variables set (e.g., `DYLD_INSERT_LIBRARIES`)
- Memory debugging tools active

**Solution:** Run the browser normally without debugging tools attached.

## "License validation failed: tampered"

The browser detected that its code has been modified.

**Causes:**
- Binary was patched or modified after compilation
- Code integrity check failed
- Memory modification detected

**Solution:** Re-download or rebuild the browser from source. Contact support if the issue persists with an unmodified binary.

## "License validation failed: clock_manipulated"

The browser detected that the system clock was set backwards.

**Causes:**
- System date was changed to an earlier time
- Time zone changes that result in apparent time regression

**Solution:** Ensure your system clock is set correctly and synced with a time server.

## "License validation failed: subscription_inactive"

Your subscription has been canceled or is no longer active.

**Causes:**
- Payment failed and subscription was canceled
- Subscription was manually canceled
- Subscription expired

**Solution:**
1. Check your subscription status with your payment provider
2. Reactivate your subscription
3. Contact support if you believe this is an error

## "License validation failed: subscription_check_failed"

The browser could not verify your subscription with the license server.

**Causes:**
- No internet connection
- License server is unreachable
- Firewall blocking the connection
- Grace period has expired

**Solution:**
1. Check your internet connection
2. Ensure firewall allows connections to the license server
3. If grace period expired, connect to the internet to re-validate
4. Contact support if the issue persists

## "License validation failed: seat_exceeded"

The maximum number of devices for this license has been reached.

**Causes:**
- You are trying to activate on a new device
- All allowed seats are already in use on other devices
- A device was reformatted/changed and is now seen as a new device

**Solution:**
1. Check how many devices are using your license in the admin portal
2. Deactivate unused devices to free up seats
3. Contact support to request additional seats
4. Upgrade to a license with more seats

---

# Support

For licensing inquiries:
- Email: support@olib.ai
- Include your license ID and hardware fingerprint when reporting issues
