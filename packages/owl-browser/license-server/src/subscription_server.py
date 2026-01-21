#!/usr/bin/env python3
"""
Owl Browser License Subscription Server

REST API server for subscription license validation.
This server handles:
- Subscription status checks (monthly validation)
- Initial license activation
- License info queries

The browser connects to this server to validate subscription licenses.
"""

import os
import sys
import json
import time
import hashlib
import secrets
from datetime import datetime, timedelta, UTC
from pathlib import Path
from functools import wraps

from flask import Flask, request, jsonify
from flask_cors import CORS


def parse_iso_datetime(dt_string: str) -> datetime:
    """Parse ISO datetime string and ensure it's timezone-aware (UTC)."""
    # Handle Z suffix
    dt_string = dt_string.replace('Z', '+00:00')
    dt = datetime.fromisoformat(dt_string)
    # If naive, assume UTC
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from config import (
    LICENSE_SERVER_HOST, LICENSE_SERVER_PORT,
    PRIVATE_KEY_PATH, PUBLIC_KEY_PATH, LICENSE_TYPES,
    NONCE_HMAC_SECRET, NONCE_TIMESTAMP_TOLERANCE,
    get_client_ip
)
from database import get_database

app = Flask(__name__)
CORS(app)

# Get database instance
db = get_database()

# RSA key for signing responses (optional, for extra security)
PRIVATE_KEY = None
if PRIVATE_KEY_PATH.exists():
    with open(PRIVATE_KEY_PATH, 'r') as f:
        PRIVATE_KEY = f.read()


# ============================================================================
# Utilities
# ============================================================================

def sign_response(data: dict) -> str:
    """Sign response data with RSA private key (if available)."""
    if not PRIVATE_KEY:
        return ""

    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding
        from cryptography.hazmat.backends import default_backend
        import base64

        # Load private key
        key = serialization.load_pem_private_key(
            PRIVATE_KEY.encode(),
            password=None,
            backend=default_backend()
        )

        # Create canonical JSON string
        data_str = json.dumps(data, sort_keys=True, separators=(',', ':'))

        # Sign
        signature = key.sign(
            data_str.encode(),
            padding.PKCS1v15(),
            hashes.SHA256()
        )

        return base64.b64encode(signature).decode()
    except Exception as e:
        print(f"Warning: Could not sign response: {e}")
        return ""


def calculate_next_check(activation_timestamp: int) -> int:
    """Calculate next monthly check timestamp based on activation date."""
    activation = datetime.fromtimestamp(activation_timestamp, tz=UTC)
    now = datetime.now(UTC)

    # Find the next occurrence of the same day/time
    next_check = activation.replace(year=now.year, month=now.month)

    # If we're past that time this month, go to next month
    if next_check <= now:
        if now.month == 12:
            next_check = next_check.replace(year=now.year + 1, month=1)
        else:
            next_check = next_check.replace(month=now.month + 1)

    return int(next_check.timestamp())


def verify_nonce(nonce: str, license_id: str) -> tuple[bool, str]:
    """
    Verify HMAC-authenticated nonce.

    Nonce format: random_hex.timestamp.hmac_hex
    - random_hex: 32 chars (16 bytes hex-encoded)
    - timestamp: Unix timestamp when nonce was generated
    - hmac_hex: 64 chars (32 bytes HMAC-SHA256 hex-encoded)

    HMAC is computed over: random_hex + timestamp + license_id

    Returns: (is_valid, error_message)
    """
    import hmac as hmac_lib

    if not nonce:
        return False, "Missing nonce"

    # Parse nonce: random.timestamp.hmac
    parts = nonce.split('.')
    if len(parts) != 3:
        return False, "Invalid nonce format"

    random_hex, timestamp_str, hmac_hex = parts

    # Validate format
    if len(random_hex) != 32:
        return False, "Invalid random component length"
    if len(hmac_hex) != 64:
        return False, "Invalid HMAC component length"

    try:
        nonce_timestamp = int(timestamp_str)
    except ValueError:
        return False, "Invalid timestamp in nonce"

    # Check timestamp is within tolerance
    now = int(time.time())
    if abs(now - nonce_timestamp) > NONCE_TIMESTAMP_TOLERANCE:
        return False, f"Nonce timestamp out of range (diff: {abs(now - nonce_timestamp)}s)"

    # Verify HMAC
    # Data to sign: random_hex + timestamp + license_id (same as browser)
    data_to_sign = f"{random_hex}{nonce_timestamp}{license_id}"

    expected_hmac = hmac_lib.new(
        NONCE_HMAC_SECRET.encode(),
        data_to_sign.encode(),
        hashlib.sha256
    ).hexdigest()

    if not hmac_lib.compare_digest(expected_hmac.lower(), hmac_hex.lower()):
        return False, "HMAC verification failed"

    # Check nonce hasn't been used before (replay protection)
    if not db.use_nonce(nonce, license_id):
        return False, "Nonce already used"

    return True, ""


# ============================================================================
# API Endpoints
# ============================================================================

@app.route('/api/v1/health', methods=['GET'])
def health_check():
    """Health check endpoint."""
    return jsonify({
        'status': 'ok',
        'service': 'owl-license-server',
        'version': '1.0.0',
        'timestamp': int(time.time())
    })


@app.route('/api/v1/debug/headers', methods=['GET'])
def debug_headers():
    """Debug endpoint to see incoming headers (remove in production)."""
    headers_dict = {
        'CF-Connecting-IP': request.headers.get('CF-Connecting-IP'),
        'X-Forwarded-For': request.headers.get('X-Forwarded-For'),
        'X-Real-IP': request.headers.get('X-Real-IP'),
        'True-Client-IP': request.headers.get('True-Client-IP'),
        'X-Forwarded-Proto': request.headers.get('X-Forwarded-Proto'),
        'CF-Ray': request.headers.get('CF-Ray'),
        'remote_addr': request.remote_addr,
        'resolved_ip': get_client_ip(request)
    }
    # Log for server-side debugging
    print(f"[DEBUG] Headers: {headers_dict}")
    return jsonify(headers_dict)


@app.route('/api/v1/subscription/check', methods=['POST'])
def check_subscription():
    """
    Check subscription status.

    Request body:
    {
        "license_id": "uuid",
        "hardware_fingerprint": "sha256-hash",
        "nonce": "random-string",
        "timestamp": unix_timestamp
    }

    Response:
    {
        "license_id": "uuid",
        "active": true/false,
        "next_check_timestamp": unix_timestamp,
        "grace_period_days": 7,
        "server_timestamp": unix_timestamp,
        "signature": "base64-signature"
    }
    """
    try:
        data = request.get_json()
        if not data:
            return jsonify({'error': 'Invalid JSON'}), 400

        license_id = data.get('license_id', '').strip()
        hardware_fp = data.get('hardware_fingerprint', '')
        # Nonce comes from X-Nonce header (HMAC-authenticated)
        nonce = request.headers.get('X-Nonce', '')
        client_timestamp = data.get('timestamp', 0)

        if not license_id:
            return jsonify({'error': 'Missing license_id'}), 400

        # Verify HMAC-authenticated nonce for replay protection and request authentication
        if nonce:
            is_valid, error_msg = verify_nonce(nonce, license_id)
            if not is_valid:
                return jsonify({'error': f'Nonce verification failed: {error_msg}', 'active': False}), 400

        # Check timestamp is recent (within 5 minutes)
        now = int(time.time())
        if client_timestamp and abs(now - client_timestamp) > 300:
            return jsonify({'error': 'Timestamp out of range'}), 400

        # Look up license in database
        license_data = db.get_license(license_id)
        if not license_data:
            # Log failed attempt
            db.log_activation(
                license_id, 'subscription_check',
                hardware_fingerprint=hardware_fp,
                ip_address=get_client_ip(request),
                user_agent=request.headers.get('User-Agent', ''),
                success=False,
                error_message='License not found'
            )
            return jsonify({'error': 'License not found', 'active': False}), 404

        # Check if license is a subscription type
        if license_data['license_type'] != 5:  # SUBSCRIPTION
            return jsonify({'error': 'Not a subscription license', 'active': False}), 400

        # Get subscription record
        subscription = db.get_subscription(license_id)
        if not subscription:
            # Create subscription record if it doesn't exist
            db.create_subscription(license_id, {
                'status': 'active',
                'customer_id': license_data.get('customer_id'),
                'plan_id': license_data.get('plan_id')
            })
            subscription = db.get_subscription(license_id)

        # Check license status
        is_active = (
            license_data['status'] == 'active' and
            subscription['status'] == 'active'
        )

        # Check expiry
        if license_data.get('expiry_date'):
            expiry = parse_iso_datetime(license_data['expiry_date'])
            if expiry < datetime.now(UTC):
                is_active = False

        # Calculate next check timestamp
        activation_ts = subscription.get('activation_date')
        if activation_ts:
            activation_dt = parse_iso_datetime(activation_ts)
            next_check = calculate_next_check(int(activation_dt.timestamp()))
        else:
            # First activation - set activation date and next check
            activation_ts = datetime.now(UTC).isoformat()
            next_check = calculate_next_check(now)
            db.update_subscription(license_id, {
                'activation_date': activation_ts,
                'last_check_date': datetime.now(UTC).isoformat(),
                'next_check_date': datetime.fromtimestamp(next_check, tz=UTC).isoformat()
            })

        # Update last check timestamp
        db.update_subscription(license_id, {
            'last_check_date': datetime.now(UTC).isoformat()
        })

        # Log successful check
        db.log_activation(
            license_id, 'subscription_check',
            hardware_fingerprint=hardware_fp,
            ip_address=get_client_ip(request),
            user_agent=request.headers.get('User-Agent', ''),
            success=True,
            customer_id=license_data.get('customer_id')
        )

        # Build response
        # Note: C++ client expects 'active' not 'is_active'
        response = {
            'license_id': license_id,
            'active': is_active,
            'next_check_timestamp': next_check,
            'grace_period_days': subscription.get('grace_period_days', 7),
            'server_timestamp': now
        }

        # Sign response
        response['signature'] = sign_response(response)

        return jsonify(response)

    except Exception as e:
        print(f"Error in subscription check: {e}")
        return jsonify({'error': 'Internal server error'}), 500


@app.route('/api/v1/subscription/activate', methods=['POST'])
def activate_subscription():
    """
    Activate a subscription license (legacy endpoint, redirects to universal activate).
    """
    return activate_license()


@app.route('/api/v1/license/activate', methods=['POST'])
def activate_license():
    """
    Universal license activation endpoint for ALL license types.

    This endpoint:
    - Tracks seat usage based on hardware fingerprint
    - Enforces max_seats limit
    - Logs activation attempts
    - Returns signed response for security

    Request body:
    {
        "license_id": "uuid",
        "hardware_fingerprint": "sha256-hash",
        "license_type": int (optional, for validation)
    }

    Response:
    {
        "license_id": "uuid",
        "activated": true/false,
        "seat_count": current_active_seats,
        "max_seats": max_allowed_seats,
        "license_type": int,
        "license_type_name": "STARTER/BUSINESS/ENTERPRISE/etc",
        "activation_timestamp": unix_timestamp,
        "next_check_timestamp": unix_timestamp (for subscriptions),
        "server_timestamp": unix_timestamp,
        "signature": "base64-rsa-signature"
    }
    """
    try:
        data = request.get_json()
        if not data:
            return jsonify({'error': 'Invalid JSON'}), 400

        license_id = data.get('license_id', '').strip()
        hardware_fp = data.get('hardware_fingerprint', '')
        device_name = data.get('device_name', '')
        os_info = data.get('os_info', '')
        browser_version = data.get('browser_version', '')
        # Nonce comes from X-Nonce header (HMAC-authenticated)
        nonce = request.headers.get('X-Nonce', '')

        if not license_id:
            return jsonify({'error': 'Missing license_id'}), 400

        if not hardware_fp:
            return jsonify({'error': 'Missing hardware_fingerprint'}), 400

        # Verify HMAC-authenticated nonce
        if nonce:
            is_valid, error_msg = verify_nonce(nonce, license_id)
            if not is_valid:
                db.log_activation(
                    license_id, 'activation',
                    hardware_fingerprint=hardware_fp,
                    ip_address=get_client_ip(request),
                    success=False,
                    error_message=f'Nonce verification failed: {error_msg}'
                )
                response = {
                    'license_id': license_id,
                    'activated': False,
                    'error': f'Nonce verification failed: {error_msg}'
                }
                response['signature'] = sign_response(response)
                return jsonify(response), 400

        # Look up license
        license_data = db.get_license(license_id)
        if not license_data:
            db.log_activation(
                license_id, 'activation',
                hardware_fingerprint=hardware_fp,
                ip_address=get_client_ip(request),
                success=False,
                error_message='License not found'
            )
            response = {
                'license_id': license_id,
                'activated': False,
                'error': 'License not found'
            }
            response['signature'] = sign_response(response)
            return jsonify(response), 404

        # Check license status
        if license_data['status'] != 'active':
            db.log_activation(
                license_id, 'activation',
                hardware_fingerprint=hardware_fp,
                ip_address=get_client_ip(request),
                success=False,
                error_message=f"License status is {license_data['status']}",
                customer_id=license_data.get('customer_id')
            )
            response = {
                'license_id': license_id,
                'activated': False,
                'error': f"License is {license_data['status']}"
            }
            response['signature'] = sign_response(response)
            return jsonify(response), 403

        # Check expiry
        if license_data.get('expiry_date'):
            try:
                expiry = parse_iso_datetime(license_data['expiry_date'])
                if expiry < datetime.now(UTC):
                    db.log_activation(
                        license_id, 'activation',
                        hardware_fingerprint=hardware_fp,
                        ip_address=get_client_ip(request),
                        success=False,
                        error_message='License expired',
                        customer_id=license_data.get('customer_id')
                    )
                    response = {
                        'license_id': license_id,
                        'activated': False,
                        'error': 'License expired'
                    }
                    response['signature'] = sign_response(response)
                    return jsonify(response), 403
            except Exception:
                pass  # Ignore parsing errors

        now = datetime.now(UTC)
        now_ts = int(now.timestamp())
        license_type = license_data['license_type']
        license_type_name = LICENSE_TYPES.get(license_type, 'UNKNOWN')
        max_seats = license_data.get('max_seats', 1)
        customer_id = license_data.get('customer_id')

        # Activate seat (handles seat counting and max_seats enforcement)
        success, error_msg, seat_count = db.activate_seat(
            license_id,
            hardware_fp,
            ip_address=get_client_ip(request),
            user_agent=request.headers.get('User-Agent', ''),
            device_name=device_name,
            os_info=os_info,
            browser_version=browser_version,
            customer_id=customer_id
        )

        if not success:
            db.log_activation(
                license_id, 'activation',
                hardware_fingerprint=hardware_fp,
                ip_address=get_client_ip(request),
                success=False,
                error_message=error_msg,
                customer_id=customer_id
            )
            response = {
                'license_id': license_id,
                'activated': False,
                'seat_count': seat_count,
                'max_seats': max_seats,
                'license_type': license_type,
                'license_type_name': license_type_name,
                'error': error_msg,
                'server_timestamp': now_ts
            }
            response['signature'] = sign_response(response)
            return jsonify(response), 403

        # Log successful activation
        db.log_activation(
            license_id, 'activation',
            hardware_fingerprint=hardware_fp,
            ip_address=get_client_ip(request),
            user_agent=request.headers.get('User-Agent', ''),
            success=True,
            customer_id=customer_id
        )

        # Build base response
        response = {
            'license_id': license_id,
            'activated': True,
            'seat_count': seat_count,
            'max_seats': max_seats,
            'license_type': license_type,
            'license_type_name': license_type_name,
            'activation_timestamp': now_ts,
            'server_timestamp': now_ts
        }

        # For subscription licenses, handle subscription-specific logic
        if license_type == 5:  # SUBSCRIPTION
            subscription = db.get_subscription(license_id)
            if not subscription:
                db.create_subscription(license_id, {
                    'status': 'active',
                    'customer_id': customer_id,
                    'plan_id': license_data.get('plan_id'),
                    'activation_date': now.isoformat(),
                    'last_check_date': now.isoformat(),
                    'next_check_date': datetime.fromtimestamp(calculate_next_check(now_ts)).isoformat()
                })
            else:
                db.update_subscription(license_id, {
                    'activation_date': now.isoformat(),
                    'last_check_date': now.isoformat(),
                    'next_check_date': datetime.fromtimestamp(calculate_next_check(now_ts)).isoformat()
                })

            response['next_check_timestamp'] = calculate_next_check(now_ts)
            response['grace_period_days'] = license_data.get('grace_period_days', 7)

        # Sign response
        response['signature'] = sign_response(response)

        return jsonify(response)

    except Exception as e:
        print(f"Error in activation: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({'error': 'Internal server error'}), 500


@app.route('/api/v1/license/info', methods=['GET'])
def license_info():
    """
    Get license information.

    Query params:
    - license_id: The license UUID

    Response:
    {
        "license_id": "uuid",
        "type": "SUBSCRIPTION",
        "name": "...",
        "organization": "...",
        "status": "active",
        "expiry_date": "iso-date",
        "is_subscription": true
    }
    """
    license_id = request.args.get('license_id', '').strip()

    if not license_id:
        return jsonify({'error': 'Missing license_id'}), 400

    license_data = db.get_license(license_id)
    if not license_data:
        return jsonify({'error': 'License not found'}), 404

    response = {
        'license_id': license_id,
        'type': LICENSE_TYPES.get(license_data['license_type'], 'UNKNOWN'),
        'name': license_data['name'],
        'organization': license_data.get('organization', ''),
        'status': license_data['status'],
        'expiry_date': license_data.get('expiry_date'),
        'is_subscription': license_data['license_type'] == 5
    }

    return jsonify(response)


# ============================================================================
# Tampering Detection Reports
# ============================================================================

# Tampering type mapping (matches browser LicenseStatus enum)
TAMPERING_TYPES = {
    'debugger': 'tampering_debugger_detected',
    'code_patched': 'tampering_code_modified',
    'clock_manipulation': 'tampering_clock_manipulation',
    'signature_invalid': 'tampering_signature_invalid',
    'hardware_mismatch': 'tampering_hardware_mismatch',
    'state_corrupted': 'tampering_state_corrupted',
    'revoked': 'tampering_license_revoked',
    'generic': 'tampering_detected'
}


@app.route('/api/v1/report-tampering', methods=['POST'])
def report_tampering():
    """
    Report tampering detection from browser client.

    This endpoint receives tampering reports when the browser detects:
    - Debugger attached
    - Code modifications/patches
    - System clock manipulation
    - Invalid license signature
    - Hardware fingerprint mismatch
    - Corrupted state files

    Request body (JSON):
    {
        "license_id": "uuid",
        "tampering_type": "debugger|code_patched|clock_manipulation|signature_invalid|hardware_mismatch|state_corrupted|generic",
        "hardware_fingerprint": "sha256-hash",
        "client_timestamp": 1234567890,
        "details": "optional additional info",
        "nonce": "random.timestamp.hmac"
    }

    Response:
    {
        "received": true,
        "server_timestamp": 1234567890
    }
    """
    data = request.get_json()
    if not data:
        return jsonify({'error': 'Invalid JSON', 'received': False}), 400

    license_id = data.get('license_id', '').strip()
    tampering_type = data.get('tampering_type', 'generic').strip().lower()
    hardware_fp = data.get('hardware_fingerprint', '').strip()
    client_timestamp = data.get('client_timestamp', 0)
    details = data.get('details', '').strip()
    nonce = data.get('nonce', '').strip()

    # Verify HMAC-authenticated nonce for request authentication
    if nonce and license_id:
        is_valid, error_msg = verify_nonce(nonce, license_id)
        if not is_valid:
            return jsonify({'error': f'Nonce verification failed: {error_msg}', 'received': False}), 400

    if not license_id:
        return jsonify({'error': 'Missing license_id', 'received': False}), 400

    # Map tampering type to action name
    action = TAMPERING_TYPES.get(tampering_type, 'tampering_detected')

    # Build error message with details
    error_parts = [f"Tampering type: {tampering_type}"]
    if client_timestamp:
        error_parts.append(f"Client timestamp: {client_timestamp}")
    if details:
        error_parts.append(f"Details: {details}")
    error_message = "; ".join(error_parts)

    # Get license info for customer_id (may not exist if license is corrupted)
    customer_id = None
    license_data = db.get_license(license_id)
    if license_data:
        customer_id = license_data.get('customer_id')

    # Log the tampering event
    db.log_activation(
        license_id,
        action,
        hardware_fingerprint=hardware_fp,
        ip_address=get_client_ip(request),
        user_agent=request.headers.get('User-Agent', ''),
        success=False,  # Tampering is always a failure
        error_message=error_message,
        customer_id=customer_id
    )

    # Log to admin audit as well for visibility
    db.log_admin_action(
        'system',
        'tampering_detected',
        target_type='license',
        target_id=license_id,
        details=f"{action}: {error_message}",
        ip_address=get_client_ip(request)
    )

    now_ts = int(datetime.now(UTC).timestamp())

    return jsonify({
        'received': True,
        'server_timestamp': now_ts
    })


# ============================================================================
# Admin API (for webhooks from payment providers)
# ============================================================================

@app.route('/api/v1/admin/subscription/cancel', methods=['POST'])
def admin_cancel_subscription():
    """
    Cancel a subscription (webhook endpoint for payment providers).

    Request body:
    {
        "license_id": "uuid",
        "api_key": "secret-key",
        "reason": "optional reason"
    }
    """
    data = request.get_json()
    if not data:
        return jsonify({'error': 'Invalid JSON'}), 400

    # Verify API key (simple auth for webhooks)
    api_key = data.get('api_key', '')
    expected_key = os.getenv('OLIB_WEBHOOK_API_KEY', '')
    if not expected_key or api_key != expected_key:
        return jsonify({'error': 'Unauthorized'}), 401

    license_id = data.get('license_id', '').strip()
    reason = data.get('reason', 'Payment provider cancellation')

    if not license_id:
        return jsonify({'error': 'Missing license_id'}), 400

    # Cancel subscription
    db.cancel_subscription(license_id)
    db.update_license(license_id, {'status': 'canceled', 'notes': reason})

    # Log action
    db.log_admin_action(
        'webhook',
        'cancel_subscription',
        target_type='subscription',
        target_id=license_id,
        details=reason,
        ip_address=get_client_ip(request)
    )

    return jsonify({'success': True, 'license_id': license_id})


@app.route('/api/v1/admin/subscription/reactivate', methods=['POST'])
def admin_reactivate_subscription():
    """
    Reactivate a canceled subscription.

    Request body:
    {
        "license_id": "uuid",
        "api_key": "secret-key"
    }
    """
    data = request.get_json()
    if not data:
        return jsonify({'error': 'Invalid JSON'}), 400

    api_key = data.get('api_key', '')
    expected_key = os.getenv('OLIB_WEBHOOK_API_KEY', '')
    if not expected_key or api_key != expected_key:
        return jsonify({'error': 'Unauthorized'}), 401

    license_id = data.get('license_id', '').strip()

    if not license_id:
        return jsonify({'error': 'Missing license_id'}), 400

    # Reactivate
    db.update_subscription(license_id, {'status': 'active', 'canceled_at': None})
    db.update_license(license_id, {'status': 'active'})

    db.log_admin_action(
        'webhook',
        'reactivate_subscription',
        target_type='subscription',
        target_id=license_id,
        ip_address=get_client_ip(request)
    )

    return jsonify({'success': True, 'license_id': license_id})


# ============================================================================
# Main
# ============================================================================

if __name__ == '__main__':
    print(f"""
╔═══════════════════════════════════════════════════════════════╗
║           Owl Browser License Subscription Server            ║
╠═══════════════════════════════════════════════════════════════╣
║  Listening on: http://{LICENSE_SERVER_HOST}:{LICENSE_SERVER_PORT}                      ║
║  Database: SQLite (license-server/data/licenses.db)           ║
║                                                               ║
║  Endpoints:                                                   ║
║    GET  /api/v1/health              - Health check            ║
║    POST /api/v1/license/activate    - Activate ANY license    ║
║    POST /api/v1/subscription/check  - Check subscription      ║
║    GET  /api/v1/license/info        - Get license info        ║
║                                                               ║
║  All responses are RSA-signed for security                    ║
╚═══════════════════════════════════════════════════════════════╝
    """)

    # Cleanup old nonces on startup
    db.cleanup_old_nonces(30)

    app.run(
        host=LICENSE_SERVER_HOST,
        port=LICENSE_SERVER_PORT,
        debug=True
    )
