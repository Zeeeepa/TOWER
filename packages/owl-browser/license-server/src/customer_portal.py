"""
Owl Browser Customer Portal

Flask Blueprint for customer self-service portal.
Customers can:
- Register using their License ID and email
- View their licenses and subscription status
- Manage their profile and address
- View billing history and invoices
- See active device seats
"""

import secrets
import hashlib
from datetime import datetime, timedelta, UTC
from functools import wraps

from flask import (
    Blueprint, render_template, request, redirect, url_for,
    flash, session, abort, make_response, jsonify
)
from werkzeug.security import generate_password_hash, check_password_hash

from database import get_database
from config import LICENSE_TYPES, get_client_ip
from email_service import EmailService

# Create blueprint
customer_bp = Blueprint('customer', __name__, url_prefix='/portal')

# Session cookie name
CUSTOMER_SESSION_COOKIE = 'customer_session'
SESSION_DURATION_HOURS = 24 * 7  # 7 days


# ============================================================================
# Helper Functions
# ============================================================================

def get_db():
    """Get database instance."""
    return get_database()


def get_email_service():
    """Get email service instance."""
    return EmailService(get_db())


def hash_token(token: str) -> str:
    """Hash a session token for storage."""
    return hashlib.sha256(token.encode()).hexdigest()


def generate_session_token() -> str:
    """Generate a secure session token."""
    return secrets.token_urlsafe(32)


def get_current_customer_user():
    """Get the current logged-in customer user from session cookie."""
    token = request.cookies.get(CUSTOMER_SESSION_COOKIE)
    if not token:
        return None

    db = get_db()
    token_hash = hash_token(token)
    session_data = db.get_customer_session_by_token(token_hash)

    if not session_data:
        return None

    if not session_data.get('user_active'):
        return None

    return session_data


def customer_login_required(f):
    """Decorator to require customer login."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        user = get_current_customer_user()
        if not user:
            flash('Please log in to access this page', 'warning')
            return redirect(url_for('customer.login', next=request.url))
        return f(*args, **kwargs)
    return decorated_function


def set_customer_session(response, user_id: str, ip_address: str = None,
                          user_agent: str = None) -> str:
    """Create session and set cookie."""
    db = get_db()
    token = generate_session_token()
    token_hash = hash_token(token)

    db.create_customer_session(
        user_id=user_id,
        token_hash=token_hash,
        ip_address=ip_address,
        user_agent=user_agent,
        expires_hours=SESSION_DURATION_HOURS
    )

    # Set cookie
    response.set_cookie(
        CUSTOMER_SESSION_COOKIE,
        token,
        max_age=SESSION_DURATION_HOURS * 3600,
        httponly=True,
        secure=request.is_secure,
        samesite='Lax'
    )

    return token


def clear_customer_session(response):
    """Clear session cookie."""
    response.delete_cookie(CUSTOMER_SESSION_COOKIE)


# ============================================================================
# Template Context
# ============================================================================

@customer_bp.context_processor
def inject_customer_user():
    """Inject current customer user into all templates."""
    user = get_current_customer_user()
    return dict(current_customer=user, license_types=LICENSE_TYPES)


# ============================================================================
# Authentication Routes
# ============================================================================

@customer_bp.route('/login', methods=['GET', 'POST'])
def login():
    """Customer login page."""
    # If already logged in, redirect to dashboard
    if get_current_customer_user():
        return redirect(url_for('customer.dashboard'))

    if request.method == 'POST':
        email = request.form.get('email', '').strip().lower()
        password = request.form.get('password', '')

        if not email or not password:
            flash('Email and password are required', 'error')
            return render_template('customer/login.html')

        db = get_db()
        user = db.get_customer_user_by_email(email)

        if not user or not check_password_hash(user['password_hash'], password):
            flash('Invalid email or password', 'error')
            return render_template('customer/login.html')

        if not user['is_active']:
            flash('Your account has been disabled', 'error')
            return render_template('customer/login.html')

        # Record login
        db.record_customer_login(user['id'], get_client_ip(request))

        # Create session
        response = make_response(redirect(
            request.args.get('next') or url_for('customer.dashboard')
        ))
        set_customer_session(
            response,
            user['id'],
            ip_address=get_client_ip(request),
            user_agent=request.headers.get('User-Agent')
        )

        flash('Welcome back!', 'success')
        return response

    return render_template('customer/login.html')


@customer_bp.route('/register', methods=['GET', 'POST'])
def register():
    """Customer registration page."""
    # If already logged in, redirect to dashboard
    if get_current_customer_user():
        return redirect(url_for('customer.dashboard'))

    if request.method == 'POST':
        license_id = request.form.get('license_id', '').strip()
        email = request.form.get('email', '').strip().lower()
        password = request.form.get('password', '')
        password_confirm = request.form.get('password_confirm', '')

        # Validation
        if not license_id or not email or not password:
            flash('All fields are required', 'error')
            return render_template('customer/register.html',
                                 form_data={'license_id': license_id, 'email': email})

        if password != password_confirm:
            flash('Passwords do not match', 'error')
            return render_template('customer/register.html',
                                 form_data={'license_id': license_id, 'email': email})

        if len(password) < 8:
            flash('Password must be at least 8 characters', 'error')
            return render_template('customer/register.html',
                                 form_data={'license_id': license_id, 'email': email})

        db = get_db()

        # Verify license
        success, error_msg, license_data = db.verify_license_for_registration(license_id, email)
        if not success:
            flash(error_msg, 'error')
            return render_template('customer/register.html',
                                 form_data={'license_id': license_id, 'email': email})

        # Check if email is already registered
        existing_user = db.get_customer_user_by_email(email)
        if existing_user:
            flash('An account with this email already exists', 'error')
            return render_template('customer/register.html',
                                 form_data={'license_id': license_id, 'email': email})

        # Create user
        try:
            user_id = db.create_customer_user({
                'license_id': license_id,
                'email': email,
                'password_hash': generate_password_hash(password),
                'customer_id': license_data.get('customer_id'),
                'is_active': True,
                'email_verified': True  # Auto-verify since they have the license
            })

            # Log activity
            db.log_customer_activity(
                customer_id=license_data.get('customer_id'),
                activity_type='registration',
                description=f'New portal account created for license {license_id[:8]}...',
                license_id=license_id,
                ip_address=get_client_ip(request),
                user_agent=request.headers.get('User-Agent')
            )

            # Auto-login
            response = make_response(redirect(url_for('customer.dashboard')))
            set_customer_session(
                response,
                user_id,
                ip_address=get_client_ip(request),
                user_agent=request.headers.get('User-Agent')
            )

            flash('Account created successfully! Welcome to Owl Browser.', 'success')
            return response

        except Exception as e:
            flash(f'Error creating account: {str(e)}', 'error')
            return render_template('customer/register.html',
                                 form_data={'license_id': license_id, 'email': email})

    return render_template('customer/register.html', form_data={})


@customer_bp.route('/logout')
def logout():
    """Log out customer."""
    user = get_current_customer_user()
    if user:
        db = get_db()
        # Delete all sessions for this user
        db.delete_customer_sessions_by_user(user['user_id'])

    response = make_response(redirect(url_for('customer.login')))
    clear_customer_session(response)
    flash('You have been logged out', 'info')
    return response


@customer_bp.route('/forgot-password', methods=['GET', 'POST'])
def forgot_password():
    """Password reset request page."""
    if request.method == 'POST':
        email = request.form.get('email', '').strip().lower()

        if not email:
            flash('Email is required', 'error')
            return render_template('customer/forgot_password.html')

        db = get_db()
        user = db.get_customer_user_by_email(email)

        # Always show success message to prevent email enumeration
        if user:
            # Generate reset token
            reset_token = secrets.token_urlsafe(32)
            expires = (datetime.now(UTC) + timedelta(hours=24)).isoformat()

            db.update_customer_user(user['id'], {
                'password_reset_token': reset_token,
                'password_reset_expires': expires
            })

            # Build reset link
            reset_link = url_for('customer.reset_password', token=reset_token, _external=True)

            # Get user details for email
            user_details = db.get_customer_user_with_details(user['id'])
            customer_name = user_details.get('license_name') or user_details.get('email', 'Customer')

            # Send password reset email
            email_service = get_email_service()
            if email_service.is_configured():
                try:
                    success, message = email_service.send_template_email(
                        template_name='password_reset',
                        to_email=email,
                        variables={
                            'customer_name': customer_name,
                            'email': email,
                            'reset_link': reset_link
                        }
                    )
                    if not success:
                        # Log the error but don't expose it to the user
                        print(f"Failed to send password reset email: {message}")
                except Exception as e:
                    print(f"Error sending password reset email: {e}")

            flash(f'Password reset instructions have been sent to {email}', 'success')

        else:
            # Same message even if user doesn't exist
            flash(f'If an account exists for {email}, reset instructions have been sent', 'success')

        return redirect(url_for('customer.login'))

    return render_template('customer/forgot_password.html')


@customer_bp.route('/reset-password/<token>', methods=['GET', 'POST'])
def reset_password(token):
    """Password reset page."""
    db = get_db()
    user = db.get_customer_user_by_reset_token(token)

    if not user:
        flash('Invalid or expired reset link', 'error')
        return redirect(url_for('customer.forgot_password'))

    # Check expiry
    if user.get('password_reset_expires'):
        expires = datetime.fromisoformat(user['password_reset_expires'].replace('Z', '+00:00'))
        if expires < datetime.now(UTC):
            flash('Reset link has expired. Please request a new one.', 'error')
            return redirect(url_for('customer.forgot_password'))

    if request.method == 'POST':
        password = request.form.get('password', '')
        password_confirm = request.form.get('password_confirm', '')

        if password != password_confirm:
            flash('Passwords do not match', 'error')
            return render_template('customer/reset_password.html', token=token)

        if len(password) < 8:
            flash('Password must be at least 8 characters', 'error')
            return render_template('customer/reset_password.html', token=token)

        # Update password and clear reset token
        db.update_customer_user(user['id'], {
            'password_hash': generate_password_hash(password),
            'password_reset_token': None,
            'password_reset_expires': None
        })

        # Clear all sessions
        db.delete_customer_sessions_by_user(user['id'])

        flash('Password updated successfully. Please log in with your new password.', 'success')
        return redirect(url_for('customer.login'))

    return render_template('customer/reset_password.html', token=token)


# ============================================================================
# Dashboard Routes
# ============================================================================

@customer_bp.route('/')
@customer_bp.route('/dashboard')
@customer_login_required
def dashboard():
    """Customer dashboard."""
    user = get_current_customer_user()
    db = get_db()

    # Get user details with license info
    user_details = db.get_customer_user_with_details(user['user_id'])

    # Get licenses
    licenses = db.get_customer_portal_licenses(user['user_id'])

    # Get subscription if applicable
    subscription = None
    if licenses and licenses[0].get('license_type') == 5:
        subscription = db.get_customer_portal_subscription(licenses[0]['id'])

    # Get recent invoices
    invoices = db.get_customer_portal_invoices(user['user_id'], limit=5)

    return render_template('customer/dashboard.html',
                         user=user_details,
                         licenses=licenses,
                         subscription=subscription,
                         invoices=invoices)


# ============================================================================
# License Routes
# ============================================================================

@customer_bp.route('/licenses')
@customer_login_required
def licenses():
    """View customer licenses."""
    user = get_current_customer_user()
    db = get_db()

    licenses = db.get_customer_portal_licenses(user['user_id'])

    return render_template('customer/licenses.html', licenses=licenses)


@customer_bp.route('/licenses/<license_id>')
@customer_login_required
def license_detail(license_id):
    """View license details."""
    user = get_current_customer_user()
    db = get_db()

    # Verify user has access to this license
    if user['license_id'] != license_id:
        abort(403)

    license_data = db.get_license(license_id)
    if not license_data:
        abort(404)

    # Get subscription if applicable
    subscription = None
    if license_data.get('license_type') == 5:
        subscription = db.get_subscription(license_id)

    # Get seats
    seats = db.get_customer_portal_seats(license_id)
    active_seat_count = db.get_active_seat_count(license_id)

    return render_template('customer/license_detail.html',
                         license=license_data,
                         subscription=subscription,
                         seats=seats,
                         active_seat_count=active_seat_count)


@customer_bp.route('/licenses/<license_id>/seats/<hardware_fingerprint>/deactivate', methods=['POST'])
@customer_login_required
def deactivate_seat(license_id, hardware_fingerprint):
    """Deactivate a device seat."""
    user = get_current_customer_user()
    db = get_db()

    # Verify user has access to this license
    if user['license_id'] != license_id:
        abort(403)

    db.deactivate_seat(license_id, hardware_fingerprint, 'Deactivated by user via portal')

    # Log activity
    db.log_customer_activity(
        customer_id=user.get('customer_id'),
        activity_type='seat_deactivation',
        description=f'Deactivated device {hardware_fingerprint[:16]}...',
        license_id=license_id,
        ip_address=get_client_ip(request)
    )

    flash('Device deactivated successfully', 'success')
    return redirect(url_for('customer.license_detail', license_id=license_id))


# ============================================================================
# Profile Routes
# ============================================================================

@customer_bp.route('/profile', methods=['GET', 'POST'])
@customer_login_required
def profile():
    """View and edit customer profile."""
    user = get_current_customer_user()
    db = get_db()

    user_details = db.get_customer_user_with_details(user['user_id'])

    if request.method == 'POST':
        # Get customer record to update
        customer_id = user_details.get('customer_id')

        if customer_id:
            # Update customer address info
            db.update_customer(customer_id, {
                'phone': request.form.get('phone', '').strip(),
                'address_line1': request.form.get('address_line1', '').strip(),
                'address_line2': request.form.get('address_line2', '').strip(),
                'city': request.form.get('city', '').strip(),
                'state': request.form.get('state', '').strip(),
                'postal_code': request.form.get('postal_code', '').strip(),
                'country': request.form.get('country', '').strip() or 'US'
            })

            flash('Profile updated successfully', 'success')

            # Refresh user details
            user_details = db.get_customer_user_with_details(user['user_id'])
        else:
            flash('Unable to update profile. No customer record found.', 'error')

    return render_template('customer/profile.html', user=user_details)


@customer_bp.route('/profile/change-password', methods=['GET', 'POST'])
@customer_login_required
def change_password():
    """Change password."""
    user = get_current_customer_user()
    db = get_db()

    if request.method == 'POST':
        current_password = request.form.get('current_password', '')
        new_password = request.form.get('new_password', '')
        confirm_password = request.form.get('confirm_password', '')

        # Get user record
        user_record = db.get_customer_user(user['user_id'])

        if not check_password_hash(user_record['password_hash'], current_password):
            flash('Current password is incorrect', 'error')
            return render_template('customer/change_password.html')

        if new_password != confirm_password:
            flash('New passwords do not match', 'error')
            return render_template('customer/change_password.html')

        if len(new_password) < 8:
            flash('Password must be at least 8 characters', 'error')
            return render_template('customer/change_password.html')

        # Update password
        db.update_customer_user(user['user_id'], {
            'password_hash': generate_password_hash(new_password)
        })

        flash('Password changed successfully', 'success')
        return redirect(url_for('customer.profile'))

    return render_template('customer/change_password.html')


# ============================================================================
# Billing Routes
# ============================================================================

@customer_bp.route('/billing')
@customer_login_required
def billing():
    """View billing history."""
    user = get_current_customer_user()
    db = get_db()

    invoices = db.get_customer_portal_invoices(user['user_id'], limit=50)

    return render_template('customer/billing.html', invoices=invoices)


@customer_bp.route('/billing/invoices/<invoice_id>')
@customer_login_required
def invoice_detail(invoice_id):
    """View invoice details."""
    user = get_current_customer_user()
    db = get_db()

    invoice = db.get_invoice(invoice_id)
    if not invoice:
        abort(404)

    # Verify access
    if invoice.get('license_id') != user['license_id'] and invoice.get('customer_id') != user.get('customer_id'):
        abort(403)

    items = db.get_invoice_items(invoice_id)

    return render_template('customer/invoice_detail.html',
                         invoice=invoice,
                         items=items)


# ============================================================================
# API Endpoints (for AJAX)
# ============================================================================

@customer_bp.route('/api/license-status')
@customer_login_required
def api_license_status():
    """Get license status (for dashboard refresh)."""
    user = get_current_customer_user()
    db = get_db()

    license_data = db.get_license(user['license_id'])
    if not license_data:
        return jsonify({'error': 'License not found'}), 404

    active_seats = db.get_active_seat_count(user['license_id'])

    return jsonify({
        'status': license_data['status'],
        'license_type': license_data['license_type'],
        'expiry_date': license_data.get('expiry_date'),
        'max_seats': license_data['max_seats'],
        'active_seats': active_seats
    })
