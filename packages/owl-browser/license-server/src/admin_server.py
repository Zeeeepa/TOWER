#!/usr/bin/env python3
"""
Owl Browser License Admin Server

A beautiful, robust admin interface for license management.
Features:
- Dashboard with statistics
- Create, view, edit, revoke licenses
- Manage subscriptions
- Customer management
- Billing plans and invoices
- Discount codes
- View audit logs
- Download license files
- Settings management
"""

import os
import sys
import json
import hashlib
import subprocess
import secrets
import uuid
from datetime import datetime, timedelta, UTC
from functools import wraps
from pathlib import Path
from decimal import Decimal

from flask import (
    Flask, render_template, request, redirect, url_for,
    flash, session, jsonify, send_file, abort, Response
)
from werkzeug.security import check_password_hash, generate_password_hash

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from config import (
    ADMIN_HOST, ADMIN_PORT, ADMIN_USERNAME, ADMIN_PASSWORD_HASH,
    ADMIN_SECRET_KEY, SESSION_LIFETIME_HOURS, LICENSE_TYPES,
    LICENSE_TYPE_IDS, LICENSES_DIR, LICENSE_GENERATOR_PATH, PROJECT_ROOT,
    get_client_ip
)
from database import get_database, LicenseDatabase
from customer_portal import customer_bp
from email_service import EmailService
from scheduler_service import start_scheduler, stop_scheduler, get_scheduler

app = Flask(__name__)
app.secret_key = ADMIN_SECRET_KEY
app.config['PERMANENT_SESSION_LIFETIME'] = timedelta(hours=SESSION_LIFETIME_HOURS)

# Register customer portal blueprint
app.register_blueprint(customer_bp)

# Get database instance
db = get_database()

# Initialize email service
email_service = EmailService(db)

# Initialize scheduler (will be started when server runs)
scheduler = None


def init_scheduler():
    """Initialize and start the scheduler if not already running."""
    global scheduler
    if scheduler is None:
        try:
            scheduler = start_scheduler(db, email_service)
            print("✅ Background scheduler started")
        except Exception as e:
            print(f"⚠️  Warning: Could not start scheduler: {str(e)}")
    return scheduler


# Start scheduler when app is imported (for WSGI deployment)
# Use @app.before_first_request equivalent for Flask 2.3+
@app.before_request
def startup_scheduler():
    """Start scheduler on first request (ensures DB is ready)."""
    global scheduler
    if scheduler is None:
        init_scheduler()
    # Remove this before_request handler after first run
    app.before_request_funcs[None].remove(startup_scheduler)


# ============================================================================
# Utility Functions
# ============================================================================

def generate_uuid():
    """Generate a new UUID."""
    return str(uuid.uuid4())


def parse_decimal(value, default=0.0):
    """Safely parse a decimal value."""
    try:
        return float(value) if value else default
    except (ValueError, TypeError):
        return default


def parse_int(value, default=0):
    """Safely parse an integer value."""
    try:
        return int(value) if value else default
    except (ValueError, TypeError):
        return default


# ============================================================================
# Authentication
# ============================================================================

def login_required(f):
    """Decorator to require login for routes."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'logged_in' not in session:
            return redirect(url_for('login'))
        return f(*args, **kwargs)
    return decorated_function


@app.route('/login', methods=['GET', 'POST'])
def login():
    """Login page."""
    if request.method == 'POST':
        username = request.form.get('username', '')
        password = request.form.get('password', '')

        # Check credentials
        if username == ADMIN_USERNAME:
            if ADMIN_PASSWORD_HASH:
                # Use hash comparison
                if check_password_hash(ADMIN_PASSWORD_HASH, password):
                    session.permanent = True
                    session['logged_in'] = True
                    session['username'] = username
                    db.log_admin_action(username, 'login', ip_address=get_client_ip(request))
                    flash('Welcome back!', 'success')
                    return redirect(url_for('dashboard'))
            else:
                # Default password for first setup
                if password == 'admin':
                    session.permanent = True
                    session['logged_in'] = True
                    session['username'] = username
                    db.log_admin_action(username, 'login', ip_address=get_client_ip(request))
                    flash('Welcome! Please change the default password in .env', 'warning')
                    return redirect(url_for('dashboard'))

        flash('Invalid username or password', 'error')
        db.log_admin_action(username, 'failed_login', ip_address=get_client_ip(request))

    return render_template('login.html')


@app.route('/logout')
def logout():
    """Logout."""
    username = session.get('username', 'unknown')
    db.log_admin_action(username, 'logout', ip_address=get_client_ip(request))
    session.clear()
    flash('You have been logged out', 'info')
    return redirect(url_for('login'))


# ============================================================================
# Dashboard
# ============================================================================

@app.route('/')
@login_required
def dashboard():
    """Main dashboard with statistics."""
    stats = db.get_statistics()
    recent_logs = db.get_admin_audit_log(limit=10)

    # Get recent licenses
    recent_licenses = db.get_all_licenses(limit=5)

    return render_template('dashboard.html',
                         stats=stats,
                         recent_logs=recent_logs,
                         recent_licenses=recent_licenses,
                         license_types=LICENSE_TYPES)


# ============================================================================
# License Management
# ============================================================================

@app.route('/licenses')
@login_required
def licenses_list():
    """List all licenses."""
    page = request.args.get('page', 1, type=int)
    status = request.args.get('status', '')
    license_type = request.args.get('type', '', type=str)

    per_page = 20
    offset = (page - 1) * per_page

    type_filter = LICENSE_TYPE_IDS.get(license_type.upper()) if license_type else None

    licenses = db.get_all_licenses(
        status=status if status else None,
        license_type=type_filter,
        limit=per_page,
        offset=offset
    )

    total = db.get_license_count(status=status if status else None)
    total_pages = (total + per_page - 1) // per_page

    return render_template('licenses.html',
                         licenses=licenses,
                         page=page,
                         total_pages=total_pages,
                         status=status,
                         license_type=license_type,
                         license_types=LICENSE_TYPES)


@app.route('/licenses/new', methods=['GET', 'POST'])
@login_required
def license_new():
    """Create a new license."""
    if request.method == 'POST':
        try:
            # Get form data
            name = request.form.get('name', '').strip()
            email = request.form.get('email', '').strip()
            organization = request.form.get('organization', '').strip()
            customer_id = request.form.get('customer_id', '').strip() or None
            plan_id = request.form.get('plan_id', '').strip() or None
            license_type = int(request.form.get('license_type', 0))
            max_seats = int(request.form.get('max_seats', 1))
            expiry_days = int(request.form.get('expiry_days', 0))
            hardware_bound = request.form.get('hardware_bound') == 'on'
            hardware_id = request.form.get('hardware_id', '').strip() if hardware_bound else ''
            grace_period = int(request.form.get('grace_period', 7))
            notes = request.form.get('notes', '').strip()

            # Version 2 Extended Metadata
            min_browser_version = request.form.get('min_browser_version', '').strip()
            max_browser_version = request.form.get('max_browser_version', '').strip()
            allowed_regions = request.form.get('allowed_regions', '').strip()
            export_control = request.form.get('export_control', '').strip()
            order_id = request.form.get('order_id', '').strip()
            invoice_id = request.form.get('invoice_id', '').strip()
            reseller_id = request.form.get('reseller_id', '').strip()
            support_tier = int(request.form.get('support_tier', 0))
            support_expiry_days = int(request.form.get('support_expiry_days', 0))
            maintenance_expiry_days = int(request.form.get('maintenance_expiry_days', 0))
            maintenance_included = request.form.get('maintenance_included') == 'on'

            if not name or not email:
                flash('Name and email are required', 'error')
                customers = db.get_all_customers()
                billing_plans = db.get_all_billing_plans(active_only=True)
                return render_template('license_form.html',
                                     license_types=LICENSE_TYPES,
                                     customers=customers,
                                     billing_plans=billing_plans,
                                     form_data=request.form)

            # Generate license using the C++ tool
            license_id = secrets.token_hex(16)
            output_path = LICENSES_DIR / f'{license_id}.olic'

            # Build command
            cmd = [
                str(LICENSE_GENERATOR_PATH),
                'generate',
                '--name', name,
                '--email', email,
                '--type', LICENSE_TYPES[license_type].lower(),
                '--seats', str(max_seats),
                '--output', str(output_path)
            ]

            if organization:
                cmd.extend(['--org', organization])
            if expiry_days > 0:
                cmd.extend(['--expiry', str(expiry_days)])
            if hardware_bound:
                cmd.append('--hardware-bound')
                if hardware_id:
                    cmd.extend(['--hardware-id', hardware_id])
            if license_type == 5:  # SUBSCRIPTION
                cmd.extend(['--grace-period', str(grace_period)])
            if notes:
                cmd.extend(['--notes', notes])

            # Version 2 Extended Metadata CLI options
            if min_browser_version:
                cmd.extend(['--min-version', min_browser_version])
            if max_browser_version:
                cmd.extend(['--max-version', max_browser_version])
            if allowed_regions:
                cmd.extend(['--regions', allowed_regions])
            if export_control:
                cmd.extend(['--export-control', export_control])
            if order_id:
                cmd.extend(['--order-id', order_id])
            if invoice_id:
                cmd.extend(['--invoice-id', invoice_id])
            if reseller_id:
                cmd.extend(['--reseller-id', reseller_id])
            if support_tier > 0:
                cmd.extend(['--support-tier', str(support_tier)])
            if maintenance_included:
                cmd.append('--maintenance')

            # Run generator
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

            if result.returncode != 0:
                flash(f'Failed to generate license: {result.stderr}', 'error')
                customers = db.get_all_customers()
                billing_plans = db.get_all_billing_plans(active_only=True)
                return render_template('license_form.html',
                                     license_types=LICENSE_TYPES,
                                     customers=customers,
                                     billing_plans=billing_plans,
                                     form_data=request.form)

            # Parse license ID from output
            for line in result.stdout.split('\n'):
                if line.startswith('License ID:'):
                    license_id = line.split(':')[1].strip()
                    break

            # Calculate expiry date
            expiry_date = None
            if expiry_days > 0:
                expiry_date = (datetime.now(UTC) + timedelta(days=expiry_days)).isoformat()

            # Calculate support expiry date
            support_expiry_date = None
            if support_expiry_days > 0:
                support_expiry_date = (datetime.now(UTC) + timedelta(days=support_expiry_days)).isoformat()
            elif expiry_date:
                support_expiry_date = expiry_date

            # Calculate maintenance expiry date
            maintenance_expiry_date = None
            if maintenance_expiry_days > 0:
                maintenance_expiry_date = (datetime.now(UTC) + timedelta(days=maintenance_expiry_days)).isoformat()
            elif expiry_date:
                maintenance_expiry_date = expiry_date

            # Save to database
            db.create_license({
                'id': license_id,
                'customer_id': customer_id,
                'plan_id': plan_id,
                'license_type': license_type,
                'name': name,
                'email': email,
                'organization': organization,
                'max_seats': max_seats,
                'expiry_date': expiry_date,
                'hardware_bound': hardware_bound,
                'hardware_fingerprint': hardware_id,
                'notes': notes,
                'file_path': str(output_path),
                'issuer': session.get('username', 'admin'),
                # Version 2 Extended Metadata
                'license_version': 2,
                'min_browser_version': min_browser_version,
                'max_browser_version': max_browser_version,
                'allowed_regions': allowed_regions,
                'export_control': export_control,
                'order_id': order_id,
                'invoice_id': invoice_id,
                'reseller_id': reseller_id,
                'support_tier': support_tier,
                'support_expiry_date': support_expiry_date,
                'maintenance_included': maintenance_included,
                'maintenance_expiry_date': maintenance_expiry_date,
                'issued_ip': get_client_ip(request)
            })

            # If subscription, create subscription record
            if license_type == 5:
                db.create_subscription(license_id, {
                    'customer_id': customer_id,
                    'plan_id': plan_id,
                    'status': 'active',
                    'grace_period_days': grace_period
                })

            # Log action
            db.log_admin_action(
                session.get('username', 'admin'),
                'create_license',
                target_type='license',
                target_id=license_id,
                details=json.dumps({'type': LICENSE_TYPES[license_type], 'email': email}),
                ip_address=get_client_ip(request)
            )

            flash(f'License created successfully! ID: {license_id}', 'success')
            return redirect(url_for('license_detail', license_id=license_id))

        except subprocess.TimeoutExpired:
            flash('License generation timed out', 'error')
        except Exception as e:
            flash(f'Error creating license: {str(e)}', 'error')

    customers = db.get_all_customers()
    billing_plans = db.get_all_billing_plans(active_only=True)
    return render_template('license_form.html',
                         license_types=LICENSE_TYPES,
                         customers=customers,
                         billing_plans=billing_plans,
                         form_data={})


@app.route('/licenses/<license_id>')
@login_required
def license_detail(license_id):
    """View license details."""
    license_data = db.get_license(license_id)
    if not license_data:
        abort(404)

    subscription = None
    if license_data['license_type'] == 5:
        subscription = db.get_subscription(license_id)

    activation_logs = db.get_activation_logs(license_id, limit=20)

    # Get seat information
    seats = db.get_license_seats(license_id)
    active_seat_count = db.get_active_seat_count(license_id)
    inactive_seat_count = db.get_inactive_seat_count(license_id)
    activation_log_count = db.get_activation_log_count(license_id)

    return render_template('license_detail.html',
                         license=license_data,
                         subscription=subscription,
                         activation_logs=activation_logs,
                         seats=seats,
                         active_seat_count=active_seat_count,
                         inactive_seat_count=inactive_seat_count,
                         activation_log_count=activation_log_count,
                         license_types=LICENSE_TYPES)


@app.route('/licenses/<license_id>/download')
@login_required
def license_download(license_id):
    """Download license file."""
    license_data = db.get_license(license_id)
    if not license_data or not license_data.get('file_path'):
        abort(404)

    file_path = Path(license_data['file_path'])
    if not file_path.exists():
        flash('License file not found on disk', 'error')
        return redirect(url_for('license_detail', license_id=license_id))

    # Log download
    db.log_admin_action(
        session.get('username', 'admin'),
        'download_license',
        target_type='license',
        target_id=license_id,
        ip_address=get_client_ip(request)
    )

    return send_file(
        file_path,
        as_attachment=True,
        download_name=f'license_{license_id[:8]}.olic'
    )


@app.route('/licenses/<license_id>/revoke', methods=['POST'])
@login_required
def license_revoke(license_id):
    """Revoke a license."""
    license_data = db.get_license(license_id)
    if not license_data:
        abort(404)

    reason = request.form.get('reason', 'No reason provided')
    db.revoke_license(license_id, reason)

    # If subscription, cancel it
    if license_data['license_type'] == 5:
        db.cancel_subscription(license_id)

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'revoke_license',
        target_type='license',
        target_id=license_id,
        details=reason,
        ip_address=get_client_ip(request)
    )

    flash('License revoked successfully', 'success')
    return redirect(url_for('license_detail', license_id=license_id))


@app.route('/licenses/<license_id>/reactivate', methods=['POST'])
@login_required
def license_reactivate(license_id):
    """Reactivate a revoked license."""
    license_data = db.get_license(license_id)
    if not license_data:
        abort(404)

    db.update_license(license_id, {'status': 'active'})

    # If subscription, reactivate it
    if license_data['license_type'] == 5:
        db.update_subscription(license_id, {
            'status': 'active',
            'canceled_at': None
        })

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'reactivate_license',
        target_type='license',
        target_id=license_id,
        ip_address=get_client_ip(request)
    )

    flash('License reactivated successfully', 'success')
    return redirect(url_for('license_detail', license_id=license_id))


@app.route('/licenses/<license_id>/reissue', methods=['POST'])
@login_required
def license_reissue(license_id):
    """Reissue a license file with the current format (regenerate .olic file)."""
    license_data = db.get_license(license_id)
    if not license_data:
        abort(404)

    try:
        # Generate new license file using existing data
        output_path = LICENSES_DIR / f'{license_id}.olic'

        # Delete old file if it exists
        if output_path.exists():
            output_path.unlink()

        # Build command with all the existing license data
        cmd = [
            str(LICENSE_GENERATOR_PATH),
            'generate',
            '--license-id', license_id,  # Use existing license ID for reissuing
            '--name', license_data['name'],
            '--email', license_data['email'],
            '--type', LICENSE_TYPES[license_data['license_type']].lower(),
            '--seats', str(license_data['max_seats']),
            '--output', str(output_path)
        ]

        if license_data.get('organization'):
            cmd.extend(['--org', license_data['organization']])

        # Calculate remaining expiry days from expiry_date
        if license_data.get('expiry_date'):
            expiry_date = license_data['expiry_date']
            if isinstance(expiry_date, str):
                expiry_date = datetime.fromisoformat(expiry_date.replace('Z', '+00:00'))
            # Ensure timezone-aware comparison
            if expiry_date.tzinfo is None:
                expiry_date = expiry_date.replace(tzinfo=UTC)
            remaining_days = (expiry_date - datetime.now(UTC)).days
            if remaining_days > 0:
                cmd.extend(['--expiry', str(remaining_days)])

        if license_data.get('hardware_bound'):
            cmd.append('--hardware-bound')
            if license_data.get('hardware_fingerprint'):
                cmd.extend(['--hardware-id', license_data['hardware_fingerprint']])

        if license_data['license_type'] == 5:  # SUBSCRIPTION
            sub_data = db.get_subscription(license_id)
            if sub_data:
                cmd.extend(['--grace-period', str(sub_data.get('grace_period_days', 7))])

        if license_data.get('notes'):
            cmd.extend(['--notes', license_data['notes']])

        # Version 2 Extended Metadata CLI options
        if license_data.get('min_browser_version'):
            cmd.extend(['--min-version', license_data['min_browser_version']])
        if license_data.get('max_browser_version'):
            cmd.extend(['--max-version', license_data['max_browser_version']])
        if license_data.get('allowed_regions'):
            cmd.extend(['--regions', license_data['allowed_regions']])
        if license_data.get('export_control'):
            cmd.extend(['--export-control', license_data['export_control']])
        if license_data.get('customer_id'):
            cmd.extend(['--customer-id', license_data['customer_id']])
        if license_data.get('plan_id'):
            cmd.extend(['--plan-id', license_data['plan_id']])
        if license_data.get('order_id'):
            cmd.extend(['--order-id', license_data['order_id']])
        if license_data.get('invoice_id'):
            cmd.extend(['--invoice-id', license_data['invoice_id']])
        if license_data.get('reseller_id'):
            cmd.extend(['--reseller-id', license_data['reseller_id']])
        if license_data.get('support_tier') and license_data['support_tier'] > 0:
            support_tiers = {1: 'basic', 2: 'standard', 3: 'premium', 4: 'enterprise'}
            cmd.extend(['--support-tier', support_tiers.get(license_data['support_tier'], 'none')])

            # Calculate remaining support expiry days
            if license_data.get('support_expiry_date'):
                support_expiry = license_data['support_expiry_date']
                if isinstance(support_expiry, str):
                    support_expiry = datetime.fromisoformat(support_expiry.replace('Z', '+00:00'))
                # Ensure timezone-aware comparison
                if support_expiry.tzinfo is None:
                    support_expiry = support_expiry.replace(tzinfo=UTC)
                support_days = (support_expiry - datetime.now(UTC)).days
                if support_days > 0:
                    cmd.extend(['--support-expiry', str(support_days)])

        if license_data.get('maintenance_included'):
            cmd.append('--maintenance')
            # Calculate remaining maintenance expiry days
            if license_data.get('maintenance_expiry_date'):
                maint_expiry = license_data['maintenance_expiry_date']
                if isinstance(maint_expiry, str):
                    maint_expiry = datetime.fromisoformat(maint_expiry.replace('Z', '+00:00'))
                # Ensure timezone-aware comparison
                if maint_expiry.tzinfo is None:
                    maint_expiry = maint_expiry.replace(tzinfo=UTC)
                maint_days = (maint_expiry - datetime.now(UTC)).days
                if maint_days > 0:
                    cmd.extend(['--maintenance-expiry', str(maint_days)])

        # Run generator
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

        if result.returncode != 0:
            flash(f'Failed to reissue license: {result.stderr}', 'error')
            return redirect(url_for('license_detail', license_id=license_id))

        # Update file path in database
        db.update_license(license_id, {
            'file_path': str(output_path),
            'license_version': 2,
            'updated_at': datetime.now(UTC).isoformat()
        })

        # Log action
        db.log_admin_action(
            session.get('username', 'admin'),
            'reissue_license',
            target_type='license',
            target_id=license_id,
            details=json.dumps({'new_file': str(output_path)}),
            ip_address=get_client_ip(request)
        )

        flash('License file reissued successfully with new format (v2)', 'success')

    except subprocess.TimeoutExpired:
        flash('License reissue timed out', 'error')
    except Exception as e:
        flash(f'Error reissuing license: {str(e)}', 'error')

    return redirect(url_for('license_detail', license_id=license_id))


# ============================================================================
# Subscriptions
# ============================================================================

@app.route('/subscriptions')
@login_required
def subscriptions_list():
    """List all subscriptions."""
    subscriptions = db.get_active_subscriptions()
    return render_template('subscriptions.html',
                         subscriptions=subscriptions,
                         license_types=LICENSE_TYPES,
                         now=datetime.now(UTC).isoformat())


@app.route('/subscriptions/<license_id>/cancel', methods=['POST'])
@login_required
def subscription_cancel(license_id):
    """Cancel a subscription."""
    db.cancel_subscription(license_id)
    db.update_license(license_id, {'status': 'canceled'})

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'cancel_subscription',
        target_type='subscription',
        target_id=license_id,
        ip_address=get_client_ip(request)
    )

    flash('Subscription canceled', 'success')
    return redirect(url_for('license_detail', license_id=license_id))


# ============================================================================
# Activations and Seats
# ============================================================================

@app.route('/activations')
@login_required
def activations_list():
    """View recent activations across all licenses."""
    activations = db.get_recent_activations_with_details(limit=100)
    total_count = db.get_activation_log_count()
    return render_template('activations.html',
                         activations=activations,
                         total_count=total_count,
                         license_types=LICENSE_TYPES)


@app.route('/seats')
@login_required
def seats_list():
    """View all active seats."""
    seats = db.get_all_active_seats(limit=200)
    inactive_count = db.get_inactive_seat_count()
    return render_template('seats.html',
                         seats=seats,
                         inactive_count=inactive_count,
                         license_types=LICENSE_TYPES)


@app.route('/licenses/<license_id>/seats/<hardware_fingerprint>/deactivate', methods=['POST'])
@login_required
def deactivate_seat(license_id, hardware_fingerprint):
    """Deactivate a specific seat."""
    db.deactivate_seat(license_id, hardware_fingerprint)

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'deactivate_seat',
        target_type='seat',
        target_id=license_id,
        details=f'Hardware: {hardware_fingerprint[:16]}...',
        ip_address=get_client_ip(request)
    )

    flash('Seat deactivated successfully', 'success')
    return redirect(url_for('license_detail', license_id=license_id))


@app.route('/seats/clear-inactive', methods=['POST'])
@login_required
def clear_inactive_seats():
    """Clear all inactive (deactivated) seats."""
    count = db.clear_inactive_seats()

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'clear_inactive_seats',
        target_type='seats',
        details=f'Deleted {count} inactive seats',
        ip_address=get_client_ip(request)
    )

    if count > 0:
        flash(f'Cleared {count} inactive seat(s)', 'success')
    else:
        flash('No inactive seats to clear', 'info')

    return redirect(url_for('seats_list'))


@app.route('/activations/clear', methods=['POST'])
@login_required
def clear_activation_logs():
    """Clear all activation logs."""
    count = db.clear_activation_logs()

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'clear_activation_logs',
        target_type='activation_logs',
        details=f'Deleted {count} activation logs',
        ip_address=get_client_ip(request)
    )

    if count > 0:
        flash(f'Cleared {count} activation log(s)', 'success')
    else:
        flash('No activation logs to clear', 'info')

    return redirect(url_for('activations_list'))


@app.route('/licenses/<license_id>/activations/clear', methods=['POST'])
@login_required
def clear_license_activation_logs(license_id):
    """Clear activation logs for a specific license."""
    count = db.clear_activation_logs(license_id=license_id)

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'clear_license_activation_logs',
        target_type='activation_logs',
        target_id=license_id,
        details=f'Deleted {count} activation logs for license',
        ip_address=get_client_ip(request)
    )

    if count > 0:
        flash(f'Cleared {count} activation log(s)', 'success')
    else:
        flash('No activation logs to clear', 'info')

    return redirect(url_for('license_detail', license_id=license_id))


@app.route('/licenses/<license_id>/seats/clear-inactive', methods=['POST'])
@login_required
def clear_license_inactive_seats(license_id):
    """Clear inactive seats for a specific license."""
    count = db.clear_inactive_seats(license_id=license_id)

    # Log action
    db.log_admin_action(
        session.get('username', 'admin'),
        'clear_license_inactive_seats',
        target_type='seats',
        target_id=license_id,
        details=f'Deleted {count} inactive seats for license',
        ip_address=get_client_ip(request)
    )

    if count > 0:
        flash(f'Cleared {count} inactive seat(s)', 'success')
    else:
        flash('No inactive seats to clear', 'info')

    return redirect(url_for('license_detail', license_id=license_id))


# ============================================================================
# Audit Logs
# ============================================================================

@app.route('/audit-log')
@login_required
def audit_log():
    """View audit log."""
    logs = db.get_admin_audit_log(limit=200)
    return render_template('audit_log.html', logs=logs)


# ============================================================================
# Customers
# ============================================================================

@app.route('/customers')
@login_required
def customers_list():
    """List all customers."""
    page = request.args.get('page', 1, type=int)
    search = request.args.get('search', '').strip()
    status = request.args.get('status', '')

    per_page = 20
    offset = (page - 1) * per_page

    customers = db.get_all_customers(
        status=status if status else None,
        search=search if search else None,
        limit=per_page,
        offset=offset
    )

    total = db.get_customer_count(status=status if status else None)
    total_pages = (total + per_page - 1) // per_page

    return render_template('customers.html',
                          customers=customers,
                          page=page,
                          total_pages=total_pages,
                          search=search,
                          status=status)


@app.route('/customers/new', methods=['GET', 'POST'])
@login_required
def customer_new():
    """Create a new customer."""
    if request.method == 'POST':
        try:
            customer_data = {
                'id': generate_uuid(),
                'email': request.form.get('email', '').strip(),
                'name': request.form.get('name', '').strip(),
                'company': request.form.get('company', '').strip(),
                'phone': request.form.get('phone', '').strip(),
                'address_line1': request.form.get('address_line1', '').strip(),
                'address_line2': request.form.get('address_line2', '').strip(),
                'city': request.form.get('city', '').strip(),
                'state': request.form.get('state', '').strip(),
                'postal_code': request.form.get('postal_code', '').strip(),
                'country': request.form.get('country', '').strip(),
                'tax_id': request.form.get('tax_id', '').strip(),
                'notes': request.form.get('notes', '').strip()
            }

            if not customer_data['email'] or not customer_data['name']:
                flash('Email and name are required', 'error')
                return render_template('customer_form.html', form_data=request.form)

            # Check if email already exists
            existing = db.get_customer_by_email(customer_data['email'])
            if existing:
                flash('A customer with this email already exists', 'error')
                return render_template('customer_form.html', form_data=request.form)

            db.create_customer(customer_data)

            db.log_admin_action(
                session.get('username', 'admin'),
                'create_customer',
                target_type='customer',
                target_id=customer_data['id'],
                details=json.dumps({'email': customer_data['email']}),
                ip_address=get_client_ip(request)
            )

            flash(f'Customer created successfully!', 'success')
            return redirect(url_for('customer_detail', customer_id=customer_data['id']))

        except Exception as e:
            flash(f'Error creating customer: {str(e)}', 'error')

    return render_template('customer_form.html', form_data={})


@app.route('/customers/<customer_id>')
@login_required
def customer_detail(customer_id):
    """View customer details."""
    customer = db.get_customer(customer_id)
    if not customer:
        abort(404)

    # Get customer's licenses
    licenses = db.get_customer_licenses(customer_id)

    # Get customer's invoices
    invoices = db.get_customer_invoices(customer_id, limit=20)

    # Get customer's payment methods
    payment_methods = db.get_customer_payment_methods(customer_id)

    # Get customer activity log
    activity = db.get_customer_activity(customer_id, limit=20)

    return render_template('customer_detail.html',
                          customer=customer,
                          licenses=licenses,
                          invoices=invoices,
                          payment_methods=payment_methods,
                          activity=activity,
                          license_types=LICENSE_TYPES)


@app.route('/customers/<customer_id>/edit', methods=['GET', 'POST'])
@login_required
def customer_edit(customer_id):
    """Edit a customer."""
    customer = db.get_customer(customer_id)
    if not customer:
        abort(404)

    if request.method == 'POST':
        try:
            updates = {
                'email': request.form.get('email', '').strip(),
                'name': request.form.get('name', '').strip(),
                'company': request.form.get('company', '').strip(),
                'phone': request.form.get('phone', '').strip(),
                'address_line1': request.form.get('address_line1', '').strip(),
                'address_line2': request.form.get('address_line2', '').strip(),
                'city': request.form.get('city', '').strip(),
                'state': request.form.get('state', '').strip(),
                'postal_code': request.form.get('postal_code', '').strip(),
                'country': request.form.get('country', '').strip(),
                'tax_id': request.form.get('tax_id', '').strip(),
                'notes': request.form.get('notes', '').strip(),
                'status': request.form.get('status', 'active')
            }

            if not updates['email'] or not updates['name']:
                flash('Email and name are required', 'error')
                return render_template('customer_form.html',
                                      form_data=request.form,
                                      customer=customer,
                                      is_edit=True)

            db.update_customer(customer_id, updates)

            db.log_admin_action(
                session.get('username', 'admin'),
                'update_customer',
                target_type='customer',
                target_id=customer_id,
                ip_address=get_client_ip(request)
            )

            flash('Customer updated successfully!', 'success')
            return redirect(url_for('customer_detail', customer_id=customer_id))

        except Exception as e:
            flash(f'Error updating customer: {str(e)}', 'error')

    return render_template('customer_form.html',
                          form_data=customer,
                          customer=customer,
                          is_edit=True)


# ============================================================================
# Billing Plans
# ============================================================================

@app.route('/billing-plans')
@login_required
def billing_plans_list():
    """List all billing plans."""
    plans = db.get_all_billing_plans(active_only=False)
    return render_template('billing_plans.html', plans=plans)


@app.route('/billing-plans/new', methods=['GET', 'POST'])
@login_required
def billing_plan_new():
    """Create a new billing plan."""
    if request.method == 'POST':
        try:
            plan_data = {
                'id': generate_uuid(),
                'name': request.form.get('name', '').strip(),
                'description': request.form.get('description', '').strip(),
                'license_type': parse_int(request.form.get('license_type')),
                'price': parse_decimal(request.form.get('price')),
                'currency': request.form.get('currency', 'USD').upper(),
                'billing_period': request.form.get('billing_period', 'monthly'),
                'max_seats': parse_int(request.form.get('max_seats'), 1),
                'features': request.form.get('features', '').strip(),
                'is_active': request.form.get('is_active') == 'on'
            }

            if not plan_data['name']:
                flash('Plan name is required', 'error')
                return render_template('billing_plan_form.html',
                                      form_data=request.form,
                                      license_types=LICENSE_TYPES)

            db.create_billing_plan(plan_data)

            db.log_admin_action(
                session.get('username', 'admin'),
                'create_billing_plan',
                target_type='billing_plan',
                target_id=plan_data['id'],
                details=json.dumps({'name': plan_data['name']}),
                ip_address=get_client_ip(request)
            )

            flash('Billing plan created successfully!', 'success')
            return redirect(url_for('billing_plans_list'))

        except Exception as e:
            flash(f'Error creating billing plan: {str(e)}', 'error')

    return render_template('billing_plan_form.html',
                          form_data={},
                          license_types=LICENSE_TYPES)


@app.route('/billing-plans/<plan_id>/edit', methods=['GET', 'POST'])
@login_required
def billing_plan_edit(plan_id):
    """Edit a billing plan."""
    plan = db.get_billing_plan(plan_id)
    if not plan:
        abort(404)

    if request.method == 'POST':
        try:
            updates = {
                'name': request.form.get('name', '').strip(),
                'description': request.form.get('description', '').strip(),
                'license_type': parse_int(request.form.get('license_type')),
                'price': parse_decimal(request.form.get('price')),
                'currency': request.form.get('currency', 'USD').upper(),
                'billing_period': request.form.get('billing_period', 'monthly'),
                'max_seats': parse_int(request.form.get('max_seats'), 1),
                'features': request.form.get('features', '').strip(),
                'is_active': request.form.get('is_active') == 'on'
            }

            if not updates['name']:
                flash('Plan name is required', 'error')
                return render_template('billing_plan_form.html',
                                      form_data=request.form,
                                      plan=plan,
                                      is_edit=True,
                                      license_types=LICENSE_TYPES)

            db.update_billing_plan(plan_id, updates)

            db.log_admin_action(
                session.get('username', 'admin'),
                'update_billing_plan',
                target_type='billing_plan',
                target_id=plan_id,
                ip_address=get_client_ip(request)
            )

            flash('Billing plan updated successfully!', 'success')
            return redirect(url_for('billing_plans_list'))

        except Exception as e:
            flash(f'Error updating billing plan: {str(e)}', 'error')

    return render_template('billing_plan_form.html',
                          form_data=plan,
                          plan=plan,
                          is_edit=True,
                          license_types=LICENSE_TYPES)


# ============================================================================
# Invoices
# ============================================================================

@app.route('/invoices')
@login_required
def invoices_list():
    """List all invoices."""
    page = request.args.get('page', 1, type=int)
    status = request.args.get('status', '')

    per_page = 20
    offset = (page - 1) * per_page

    invoices = db.get_all_invoices(
        status=status if status else None,
        limit=per_page,
        offset=offset
    )

    total = db.get_invoice_count(status=status if status else None)
    total_pages = (total + per_page - 1) // per_page

    return render_template('invoices.html',
                          invoices=invoices,
                          page=page,
                          total_pages=total_pages,
                          status=status)


@app.route('/invoices/<invoice_id>')
@login_required
def invoice_detail(invoice_id):
    """View invoice details."""
    invoice = db.get_invoice(invoice_id)
    if not invoice:
        abort(404)

    # Get invoice items
    items = db.get_invoice_items(invoice_id)

    # Get customer
    customer = None
    if invoice.get('customer_id'):
        customer = db.get_customer(invoice['customer_id'])

    return render_template('invoice_detail.html',
                          invoice=invoice,
                          items=items,
                          customer=customer)


@app.route('/invoices/<invoice_id>/mark-paid', methods=['POST'])
@login_required
def invoice_mark_paid(invoice_id):
    """Mark an invoice as paid."""
    invoice = db.get_invoice(invoice_id)
    if not invoice:
        abort(404)

    payment_method = request.form.get('payment_method', 'manual')
    payment_ref = request.form.get('payment_ref', '')

    db.mark_invoice_paid(invoice_id, payment_method, payment_ref)

    # Reactivate suspended license if applicable
    sched = get_scheduler()
    if sched:
        try:
            sched.reactivate_license_on_payment(invoice_id)
        except Exception as e:
            flash(f'Warning: Could not reactivate license: {str(e)}', 'warning')

    db.log_admin_action(
        session.get('username', 'admin'),
        'mark_invoice_paid',
        target_type='invoice',
        target_id=invoice_id,
        details=json.dumps({'method': payment_method, 'ref': payment_ref}),
        ip_address=get_client_ip(request)
    )

    flash('Invoice marked as paid', 'success')
    return redirect(url_for('invoice_detail', invoice_id=invoice_id))


@app.route('/invoices/<invoice_id>/void', methods=['POST'])
@login_required
def invoice_void(invoice_id):
    """Void an invoice."""
    invoice = db.get_invoice(invoice_id)
    if not invoice:
        abort(404)

    db.void_invoice(invoice_id)

    db.log_admin_action(
        session.get('username', 'admin'),
        'void_invoice',
        target_type='invoice',
        target_id=invoice_id,
        ip_address=get_client_ip(request)
    )

    flash('Invoice voided', 'success')
    return redirect(url_for('invoice_detail', invoice_id=invoice_id))


@app.route('/customers/<customer_id>/invoices/new', methods=['GET', 'POST'])
@login_required
def invoice_new(customer_id):
    """Create a new invoice for a customer."""
    customer = db.get_customer(customer_id)
    if not customer:
        abort(404)

    # Get customer's licenses and billing plans for selection
    customer_licenses = db.get_customer_licenses(customer_id)
    billing_plans = db.get_all_billing_plans(active_only=True)
    discount_codes = db.get_all_discount_codes(active_only=True)

    # Create a plan lookup for quick access
    plans_by_id = {plan['id']: plan for plan in billing_plans}

    # Enrich licenses with plan pricing info
    for license in customer_licenses:
        if license.get('plan_id') and license['plan_id'] in plans_by_id:
            plan = plans_by_id[license['plan_id']]
            license['plan_name'] = plan.get('display_name') or plan.get('name')
            license['price_monthly'] = plan.get('price_monthly', 0)
            license['price_yearly'] = plan.get('price_yearly', 0)
        else:
            license['plan_name'] = None
            license['price_monthly'] = 0
            license['price_yearly'] = 0

    if request.method == 'POST':
        try:
            # Parse invoice items
            items = []
            item_descriptions = request.form.getlist('item_description[]')
            item_quantities = request.form.getlist('item_quantity[]')
            item_prices = request.form.getlist('item_price[]')
            item_license_ids = request.form.getlist('item_license_id[]')
            item_plan_ids = request.form.getlist('item_plan_id[]')

            for i in range(len(item_descriptions)):
                if item_descriptions[i].strip():
                    items.append({
                        'description': item_descriptions[i].strip(),
                        'quantity': parse_int(item_quantities[i], 1),
                        'unit_price': parse_decimal(item_prices[i]),
                        'license_id': item_license_ids[i] if i < len(item_license_ids) and item_license_ids[i] else None,
                        'plan_id': item_plan_ids[i] if i < len(item_plan_ids) and item_plan_ids[i] else None
                    })

            if not items:
                flash('At least one invoice item is required', 'error')
                return render_template('invoice_form.html',
                                      customer=customer,
                                      licenses=customer_licenses,
                                      billing_plans=billing_plans,
                                      discount_codes=discount_codes,
                                      license_types=LICENSE_TYPES,
                                      form_data=request.form)

            # Calculate totals
            subtotal = sum(item['quantity'] * item['unit_price'] for item in items)
            tax_rate = parse_decimal(request.form.get('tax_rate'))
            tax_amount = subtotal * (tax_rate / 100)

            # Handle discount code
            discount_code = request.form.get('discount_code', '').strip()
            discount_amount = 0
            if discount_code:
                code_data = db.get_discount_code(discount_code)
                if code_data and code_data.get('is_active'):
                    if code_data['discount_type'] == 'percentage':
                        discount_amount = subtotal * (code_data['discount_value'] / 100)
                    else:
                        discount_amount = code_data['discount_value']
            else:
                discount_amount = parse_decimal(request.form.get('discount_amount', 0))

            total = subtotal + tax_amount - discount_amount

            # Create invoice
            invoice_data = {
                'id': generate_uuid(),
                'customer_id': customer_id,
                'invoice_number': db.generate_invoice_number(),
                'status': 'pending',
                'subtotal': subtotal,
                'tax_rate': tax_rate,
                'tax_amount': tax_amount,
                'discount_amount': discount_amount,
                'discount_code': discount_code if discount_code else None,
                'total': total,
                'currency': request.form.get('currency', 'USD'),
                'notes': request.form.get('notes', '').strip(),
                'due_date': request.form.get('due_date') or None
            }

            invoice_id = db.create_invoice(invoice_data)

            # Add invoice items
            for item in items:
                item['invoice_id'] = invoice_id
                item['id'] = generate_uuid()
                item['amount'] = item['quantity'] * item['unit_price']
                db.create_invoice_item(item)

            # Increment discount code usage if used
            if discount_code:
                db.use_discount_code(discount_code)

            db.log_admin_action(
                session.get('username', 'admin'),
                'create_invoice',
                target_type='invoice',
                target_id=invoice_id,
                details=json.dumps({'customer': customer_id, 'total': total}),
                ip_address=get_client_ip(request)
            )

            # Send invoice created email notification
            if email_service.is_configured():
                try:
                    invoice = db.get_invoice(invoice_id)
                    if invoice and invoice.get('status') == 'pending':
                        success, msg = email_service.send_invoice_created(invoice, customer)
                        if success:
                            flash('Invoice created and email notification sent!', 'success')
                        else:
                            flash(f'Invoice created, but email failed: {msg}', 'warning')
                    else:
                        flash('Invoice created successfully!', 'success')
                except Exception as e:
                    flash(f'Invoice created, but email error: {str(e)}', 'warning')
            else:
                flash('Invoice created successfully!', 'success')

            return redirect(url_for('invoice_detail', invoice_id=invoice_id))

        except Exception as e:
            flash(f'Error creating invoice: {str(e)}', 'error')

    return render_template('invoice_form.html',
                          customer=customer,
                          licenses=customer_licenses,
                          billing_plans=billing_plans,
                          discount_codes=discount_codes,
                          license_types=LICENSE_TYPES,
                          form_data={})


# ============================================================================
# Discount Codes
# ============================================================================

@app.route('/discount-codes')
@login_required
def discount_codes_list():
    """List all discount codes."""
    codes = db.get_all_discount_codes()
    return render_template('discount_codes.html', codes=codes)


@app.route('/discount-codes/new', methods=['GET', 'POST'])
@login_required
def discount_code_new():
    """Create a new discount code."""
    if request.method == 'POST':
        try:
            code_data = {
                'id': generate_uuid(),
                'code': request.form.get('code', '').strip().upper(),
                'description': request.form.get('description', '').strip(),
                'discount_type': request.form.get('discount_type', 'percentage'),
                'discount_value': parse_decimal(request.form.get('discount_value')),
                'max_uses': parse_int(request.form.get('max_uses')) or None,
                'valid_from': request.form.get('valid_from') or None,
                'valid_until': request.form.get('valid_until') or None,
                'applicable_plans': request.form.get('applicable_plans', '').strip(),
                'is_active': request.form.get('is_active') == 'on'
            }

            if not code_data['code']:
                flash('Discount code is required', 'error')
                return render_template('discount_code_form.html', form_data=request.form)

            # Check if code already exists
            existing = db.get_discount_code_by_code(code_data['code'])
            if existing:
                flash('A discount code with this name already exists', 'error')
                return render_template('discount_code_form.html', form_data=request.form)

            db.create_discount_code(code_data)

            db.log_admin_action(
                session.get('username', 'admin'),
                'create_discount_code',
                target_type='discount_code',
                target_id=code_data['id'],
                details=json.dumps({'code': code_data['code']}),
                ip_address=get_client_ip(request)
            )

            flash('Discount code created successfully!', 'success')
            return redirect(url_for('discount_codes_list'))

        except Exception as e:
            flash(f'Error creating discount code: {str(e)}', 'error')

    return render_template('discount_code_form.html', form_data={})


@app.route('/discount-codes/<code_id>/edit', methods=['GET', 'POST'])
@login_required
def discount_code_edit(code_id):
    """Edit a discount code."""
    code = db.get_discount_code(code_id)
    if not code:
        abort(404)

    if request.method == 'POST':
        try:
            updates = {
                'code': request.form.get('code', '').strip().upper(),
                'description': request.form.get('description', '').strip(),
                'discount_type': request.form.get('discount_type', 'percentage'),
                'discount_value': parse_decimal(request.form.get('discount_value')),
                'max_uses': parse_int(request.form.get('max_uses')) or None,
                'valid_from': request.form.get('valid_from') or None,
                'valid_until': request.form.get('valid_until') or None,
                'applicable_plans': request.form.get('applicable_plans', '').strip(),
                'is_active': request.form.get('is_active') == 'on'
            }

            if not updates['code']:
                flash('Discount code is required', 'error')
                return render_template('discount_code_form.html',
                                      form_data=request.form,
                                      discount_code=code,
                                      is_edit=True)

            db.update_discount_code(code_id, updates)

            db.log_admin_action(
                session.get('username', 'admin'),
                'update_discount_code',
                target_type='discount_code',
                target_id=code_id,
                ip_address=get_client_ip(request)
            )

            flash('Discount code updated successfully!', 'success')
            return redirect(url_for('discount_codes_list'))

        except Exception as e:
            flash(f'Error updating discount code: {str(e)}', 'error')

    return render_template('discount_code_form.html',
                          form_data=code,
                          discount_code=code,
                          is_edit=True)


@app.route('/discount-codes/<code_id>/delete', methods=['POST'])
@login_required
def discount_code_delete(code_id):
    """Delete a discount code."""
    code = db.get_discount_code(code_id)
    if not code:
        abort(404)

    db.delete_discount_code(code_id)

    db.log_admin_action(
        session.get('username', 'admin'),
        'delete_discount_code',
        target_type='discount_code',
        target_id=code_id,
        details=json.dumps({'code': code['code']}),
        ip_address=get_client_ip(request)
    )

    flash('Discount code deleted', 'success')
    return redirect(url_for('discount_codes_list'))


# ============================================================================
# Settings
# ============================================================================

@app.route('/settings')
@login_required
def settings_page():
    """View and manage settings."""
    settings = db.get_all_settings()
    return render_template('settings.html', settings=settings)


@app.route('/settings/update', methods=['POST'])
@login_required
def settings_update():
    """Update settings."""
    try:
        # Process all settings from the form
        for key in request.form:
            if key.startswith('setting_'):
                setting_key = key[8:]  # Remove 'setting_' prefix
                value = request.form.get(key, '').strip()
                db.set_setting(setting_key, value)

        db.log_admin_action(
            session.get('username', 'admin'),
            'update_settings',
            ip_address=get_client_ip(request)
        )

        flash('Settings updated successfully!', 'success')
    except Exception as e:
        flash(f'Error updating settings: {str(e)}', 'error')

    return redirect(url_for('settings_page'))


# ============================================================================
# Email Templates
# ============================================================================

@app.route('/email-templates')
@login_required
def email_templates_list():
    """List all email templates."""
    templates = db.get_all_email_templates()
    return render_template('email_templates.html', templates=templates)


@app.route('/email-templates/<template_id>/edit', methods=['GET', 'POST'])
@login_required
def email_template_edit(template_id):
    """Edit an email template."""
    template = db.get_email_template_by_id(template_id)
    if not template:
        abort(404)

    if request.method == 'POST':
        try:
            updates = {
                'subject': request.form.get('subject', '').strip(),
                'body_html': request.form.get('body_html', '').strip(),
                'body_text': request.form.get('body_text', '').strip(),
                'is_active': request.form.get('is_active') == 'on'
            }

            db.update_email_template(template_id, updates)

            db.log_admin_action(
                session.get('username', 'admin'),
                'update_email_template',
                target_type='email_template',
                target_id=template_id,
                ip_address=get_client_ip(request)
            )

            flash('Email template updated successfully!', 'success')
            return redirect(url_for('email_templates_list'))

        except Exception as e:
            flash(f'Error updating email template: {str(e)}', 'error')

    return render_template('email_template_form.html',
                          template=template)


# ============================================================================
# Billing History
# ============================================================================

@app.route('/billing-history')
@login_required
def billing_history_list():
    """View billing history."""
    page = request.args.get('page', 1, type=int)
    per_page = 50
    offset = (page - 1) * per_page

    history = db.get_billing_history(limit=per_page, offset=offset)
    total = db.get_billing_history_count()
    total_pages = (total + per_page - 1) // per_page

    return render_template('billing_history.html',
                          history=history,
                          page=page,
                          total_pages=total_pages)


# ============================================================================
# API Endpoints
# ============================================================================

@app.route('/api/stats')
@login_required
def api_stats():
    """Get statistics as JSON."""
    return jsonify(db.get_statistics())


@app.route('/api/licenses/<license_id>/verify')
@login_required
def api_verify_license(license_id):
    """Verify a license using the generator tool."""
    license_data = db.get_license(license_id)
    if not license_data or not license_data.get('file_path'):
        return jsonify({'valid': False, 'error': 'License not found'})

    file_path = Path(license_data['file_path'])
    if not file_path.exists():
        return jsonify({'valid': False, 'error': 'License file not found'})

    try:
        result = subprocess.run(
            [str(LICENSE_GENERATOR_PATH), 'verify', str(file_path)],
            capture_output=True,
            text=True,
            timeout=10
        )
        return jsonify({
            'valid': result.returncode == 0,
            'output': result.stdout,
            'error': result.stderr if result.returncode != 0 else None
        })
    except Exception as e:
        return jsonify({'valid': False, 'error': str(e)})


# ============================================================================
# Error Handlers
# ============================================================================

@app.errorhandler(404)
def not_found(e):
    """404 error handler."""
    return render_template('error.html',
                         error_code=404,
                         error_message='Page not found'), 404


@app.errorhandler(500)
def server_error(e):
    """500 error handler."""
    return render_template('error.html',
                         error_code=500,
                         error_message='Internal server error'), 500


# ============================================================================
# Template Filters
# ============================================================================

@app.template_filter('datetime')
def format_datetime(value):
    """Format datetime string."""
    if not value:
        return 'N/A'
    try:
        if isinstance(value, str):
            dt = datetime.fromisoformat(value.replace('Z', '+00:00'))
        else:
            dt = value
        return dt.strftime('%Y-%m-%d %H:%M')
    except Exception:
        return value


@app.template_filter('date')
def format_date(value):
    """Format date only (handles both strings and datetime objects)."""
    if not value:
        return 'N/A'
    try:
        if isinstance(value, str):
            return value[:10]
        elif hasattr(value, 'strftime'):
            return value.strftime('%Y-%m-%d')
        else:
            return str(value)[:10]
    except Exception:
        return str(value)


@app.template_filter('license_type_name')
def license_type_name(value):
    """Get license type name."""
    return LICENSE_TYPES.get(value, 'Unknown')


@app.template_filter('status_badge')
def status_badge(status):
    """Get Bootstrap badge class for status."""
    badges = {
        'active': 'success',
        'expired': 'warning',
        'revoked': 'danger',
        'deleted': 'secondary',
        'canceled': 'danger',
        'pending': 'info'
    }
    return badges.get(status, 'secondary')


# ============================================================================
# Main
# ============================================================================

def start_background_scheduler():
    """Start the background scheduler for automated tasks."""
    sched = init_scheduler()
    if sched:
        jobs = sched.get_jobs_status()
        print(f"   Scheduled jobs: {len(jobs)}")
        for job in jobs:
            print(f"   - {job['name']}: next run at {job['next_run']}")


if __name__ == '__main__':
    print(f"""
╔═══════════════════════════════════════════════════════════════╗
║           Owl Browser License Admin Server                   ║
╠═══════════════════════════════════════════════════════════════╣
║  URL: http://{ADMIN_HOST}:{ADMIN_PORT}                              ║
║  Username: {ADMIN_USERNAME}                                          ║
║  Password: {'*' * len(ADMIN_PASSWORD_HASH) if ADMIN_PASSWORD_HASH else 'admin (default)'}              ║
╚═══════════════════════════════════════════════════════════════╝
    """)

    # Check if license generator exists
    if not LICENSE_GENERATOR_PATH.exists():
        print(f"⚠️  Warning: License generator not found at {LICENSE_GENERATOR_PATH}")
        print("   Run 'npm run build' to build it first.")

    # Start background scheduler for automated tasks
    start_background_scheduler()

    try:
        app.run(
            host=ADMIN_HOST,
            port=ADMIN_PORT,
            debug=True,
            use_reloader=False  # Disable reloader to prevent duplicate scheduler
        )
    finally:
        # Stop scheduler on shutdown
        stop_scheduler()
