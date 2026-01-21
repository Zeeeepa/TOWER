"""
Owl Browser License Server Configuration

Centralized configuration for both admin and subscription servers.
"""

import os
from pathlib import Path
from dotenv import load_dotenv

# Load environment variables from .env file
env_path = Path(__file__).parent.parent / '.env'
load_dotenv(env_path)

# Base paths
BASE_DIR = Path(__file__).parent
PROJECT_ROOT = BASE_DIR.parent
LICENSES_DIR = BASE_DIR / 'licenses'
DATABASE_DIR = BASE_DIR / 'data'

# Ensure directories exist
LICENSES_DIR.mkdir(exist_ok=True)
DATABASE_DIR.mkdir(exist_ok=True)

# Database configuration
DATABASE_TYPE = os.getenv('OLIB_DB_TYPE', 'sqlite')  # sqlite, postgresql, mysql
DATABASE_URL = os.getenv('OLIB_DB_URL', str(DATABASE_DIR / 'licenses.db'))

# PostgreSQL/MySQL config (when not using SQLite)
DB_HOST = os.getenv('OLIB_DB_HOST', 'localhost')
DB_PORT = int(os.getenv('OLIB_DB_PORT', '5432'))
DB_NAME = os.getenv('OLIB_DB_NAME', 'olib_licenses')
DB_USER = os.getenv('OLIB_DB_USER', 'olib')
DB_PASSWORD = os.getenv('OLIB_DB_PASSWORD', '')

# Admin server configuration
ADMIN_HOST = os.getenv('OLIB_ADMIN_HOST', '127.0.0.1')
ADMIN_PORT = int(os.getenv('OLIB_ADMIN_PORT', '3035'))
ADMIN_USERNAME = os.getenv('OLIB_ADMIN_USERNAME', 'admin')
ADMIN_PASSWORD_HASH = os.getenv('OLIB_ADMIN_PASSWORD_HASH', '')
ADMIN_SECRET_KEY = os.getenv('OLIB_ADMIN_SECRET_KEY', 'change-this-in-production-to-random-string')

# License server configuration
LICENSE_SERVER_HOST = os.getenv('OLIB_LICENSE_SERVER_HOST', '0.0.0.0')
LICENSE_SERVER_PORT = int(os.getenv('OLIB_LICENSE_SERVER_PORT', '3034'))

# License generator path (Release build on macOS)
LICENSE_GENERATOR_PATH = PROJECT_ROOT / 'build' / 'Release' / 'license_generator'
if not LICENSE_GENERATOR_PATH.exists():
    # Fallback for other build configurations
    LICENSE_GENERATOR_PATH = PROJECT_ROOT / 'build' / 'license_generator'
if not LICENSE_GENERATOR_PATH.exists():
    # Docker deployment path (license_generator is in /app/)
    LICENSE_GENERATOR_PATH = BASE_DIR / 'license_generator'
if not LICENSE_GENERATOR_PATH.exists():
    # Legacy Docker path
    LICENSE_GENERATOR_PATH = PROJECT_ROOT / 'license_generator'

# RSA key paths (for license signing)
# First check the standard ~/.owl_license directory, then fallback to local keys/
_HOME_KEY_DIR = Path.home() / '.owl_license'
if (_HOME_KEY_DIR / 'owl_license.key').exists():
    PRIVATE_KEY_PATH = _HOME_KEY_DIR / 'owl_license.key'
    PUBLIC_KEY_PATH = _HOME_KEY_DIR / 'owl_license.pub'
else:
    PRIVATE_KEY_PATH = BASE_DIR / 'keys' / 'private_key.pem'
    PUBLIC_KEY_PATH = BASE_DIR / 'keys' / 'public_key.pem'

# Session configuration
SESSION_LIFETIME_HOURS = 24

# HMAC secret for nonce authentication (shared with browser)
NONCE_HMAC_SECRET = os.getenv('OWL_NONCE_HMAC_SECRET', 'change-this-in-production-insecure-default-key!')

# Nonce timestamp tolerance (seconds) - requests must be within this time window
NONCE_TIMESTAMP_TOLERANCE = 300  # 5 minutes

# License types mapping
LICENSE_TYPES = {
    0: 'TRIAL',
    1: 'STARTER',      # Monthly subscription ($1,999/mo, 3 seats)
    2: 'BUSINESS',     # One-time $19,999 + optional $3,999/mo maintenance (10 seats, 1 year)
    3: 'ENTERPRISE',   # One-time $49,999 + optional $9,999/mo maintenance (50 seats, 1 year)
    4: 'DEVELOPER',
    5: 'SUBSCRIPTION'
}

LICENSE_TYPE_IDS = {v: k for k, v in LICENSE_TYPES.items()}


def get_client_ip(request) -> str:
    """
    Get the real client IP address from a Flask request.

    Checks proxy headers in order of preference:
    1. CF-Connecting-IP (Cloudflare)
    2. X-Forwarded-For (standard proxy header, takes first/leftmost IP)
    3. X-Real-IP (nginx)
    4. Falls back to request.remote_addr

    Args:
        request: Flask request object

    Returns:
        Client IP address string
    """
    # Cloudflare
    if request.headers.get('CF-Connecting-IP'):
        return request.headers.get('CF-Connecting-IP')

    # Standard proxy header (can contain multiple IPs: "client, proxy1, proxy2")
    forwarded_for = request.headers.get('X-Forwarded-For')
    if forwarded_for:
        # Take the first (leftmost) IP which is the original client
        return forwarded_for.split(',')[0].strip()

    # Nginx proxy
    if request.headers.get('X-Real-IP'):
        return request.headers.get('X-Real-IP')

    # Direct connection
    return request.remote_addr
