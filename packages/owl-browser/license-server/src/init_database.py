#!/usr/bin/env python3
"""
Owl Browser License Database Initialization Script

Creates a fresh database with all required tables and indexes.
Supports SQLite, PostgreSQL, and MySQL backends.

Database Schema:
================
Core Tables:
  - customers          : Customer/account management
  - licenses           : License records (linked to customers)
  - subscriptions      : Subscription details for licenses
  - license_seats      : Active device seats per license

Billing Tables:
  - billing_plans      : Pricing plans and tiers
  - payment_methods    : Customer payment methods
  - invoices           : Invoice records
  - billing_history    : Payment transaction history

Admin & Audit Tables:
  - admin_users        : Admin user accounts
  - admin_audit_log    : Admin action audit trail
  - activation_logs    : License activation attempts
  - used_nonces        : Replay attack protection

Usage:
    python3 init_database.py                    # Initialize with current config
    python3 init_database.py --force            # Drop and recreate (destructive!)
    python3 init_database.py --backup           # Backup before initializing
    python3 init_database.py --backup --force   # Backup then drop and recreate
    python3 init_database.py --check            # Just check if database exists and show info
"""

import os
import sys
import shutil
import argparse
from datetime import datetime, UTC
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from config import (
    DATABASE_TYPE, DATABASE_URL, DB_HOST, DB_PORT,
    DB_NAME, DB_USER, DB_PASSWORD, DATABASE_DIR
)


# ============================================================================
# SQLite Schema
# ============================================================================

def get_sqlite_schema() -> list:
    """Get SQLite-compatible CREATE TABLE statements."""
    return [
        # ==================== Admin Users ====================
        '''
        CREATE TABLE IF NOT EXISTS admin_users (
            id TEXT PRIMARY KEY,
            username TEXT NOT NULL UNIQUE,
            email TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            role TEXT DEFAULT 'admin',
            is_active INTEGER DEFAULT 1,
            last_login_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
        ''',

        # ==================== Billing Plans ====================
        '''
        CREATE TABLE IF NOT EXISTS billing_plans (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            display_name TEXT NOT NULL,
            description TEXT,
            license_type INTEGER NOT NULL,
            price_monthly REAL DEFAULT 0,
            price_yearly REAL DEFAULT 0,
            price_lifetime REAL DEFAULT 0,
            currency TEXT DEFAULT 'USD',
            max_seats INTEGER DEFAULT 1,
            features TEXT,
            is_active INTEGER DEFAULT 1,
            sort_order INTEGER DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
        ''',

        # ==================== Customers ====================
        '''
        CREATE TABLE IF NOT EXISTS customers (
            id TEXT PRIMARY KEY,
            email TEXT NOT NULL UNIQUE,
            name TEXT NOT NULL,
            company TEXT,
            phone TEXT,
            address_line1 TEXT,
            address_line2 TEXT,
            city TEXT,
            state TEXT,
            postal_code TEXT,
            country TEXT DEFAULT 'US',
            tax_id TEXT,
            stripe_customer_id TEXT,
            paypal_customer_id TEXT,
            notes TEXT,
            status TEXT DEFAULT 'active',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
        ''',

        # ==================== Licenses (must come before customer_users) ====================
        '''
        CREATE TABLE IF NOT EXISTS licenses (
            id TEXT PRIMARY KEY,
            customer_id TEXT,
            plan_id TEXT,
            license_type INTEGER NOT NULL,
            name TEXT NOT NULL,
            email TEXT NOT NULL,
            organization TEXT,
            max_seats INTEGER DEFAULT 1,
            issue_date TEXT NOT NULL,
            expiry_date TEXT,
            hardware_bound INTEGER DEFAULT 0,
            hardware_fingerprint TEXT,
            feature_flags INTEGER DEFAULT 0,
            custom_data TEXT,
            issuer TEXT,
            notes TEXT,
            file_path TEXT,
            status TEXT DEFAULT 'active',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            -- Version 2 Extended Metadata
            license_version INTEGER DEFAULT 2,
            min_browser_version TEXT,
            max_browser_version TEXT,
            allowed_regions TEXT,
            export_control TEXT,
            total_activations INTEGER DEFAULT 0,
            last_device_name TEXT,
            order_id TEXT,
            invoice_id TEXT,
            reseller_id TEXT,
            support_tier INTEGER DEFAULT 0,
            support_expiry_date TEXT,
            revocation_check_url TEXT,
            issued_ip TEXT,
            maintenance_included INTEGER DEFAULT 0,
            maintenance_expiry_date TEXT,
            FOREIGN KEY (customer_id) REFERENCES customers(id),
            FOREIGN KEY (plan_id) REFERENCES billing_plans(id)
        )
        ''',

        # ==================== Customer Users (Portal Login) ====================
        '''
        CREATE TABLE IF NOT EXISTS customer_users (
            id TEXT PRIMARY KEY,
            license_id TEXT NOT NULL UNIQUE,
            email TEXT NOT NULL,
            password_hash TEXT NOT NULL,
            customer_id TEXT,
            is_active INTEGER DEFAULT 1,
            email_verified INTEGER DEFAULT 0,
            email_verification_token TEXT,
            email_verification_expires TEXT,
            password_reset_token TEXT,
            password_reset_expires TEXT,
            last_login_at TEXT,
            last_login_ip TEXT,
            login_count INTEGER DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id),
            FOREIGN KEY (customer_id) REFERENCES customers(id)
        )
        ''',

        # ==================== Customer Sessions ====================
        '''
        CREATE TABLE IF NOT EXISTS customer_sessions (
            id TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            token_hash TEXT NOT NULL,
            ip_address TEXT,
            user_agent TEXT,
            expires_at TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY (user_id) REFERENCES customer_users(id)
        )
        ''',

        # ==================== Payment Methods ====================
        '''
        CREATE TABLE IF NOT EXISTS payment_methods (
            id TEXT PRIMARY KEY,
            customer_id TEXT NOT NULL,
            type TEXT NOT NULL,
            provider TEXT NOT NULL,
            provider_payment_id TEXT,
            last_four TEXT,
            brand TEXT,
            exp_month INTEGER,
            exp_year INTEGER,
            is_default INTEGER DEFAULT 0,
            is_active INTEGER DEFAULT 1,
            billing_name TEXT,
            billing_email TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id)
        )
        ''',

        # ==================== Subscriptions ====================
        '''
        CREATE TABLE IF NOT EXISTS subscriptions (
            id TEXT PRIMARY KEY,
            license_id TEXT NOT NULL UNIQUE,
            customer_id TEXT,
            plan_id TEXT,
            status TEXT DEFAULT 'active',
            billing_cycle TEXT DEFAULT 'monthly',
            current_period_start TEXT,
            current_period_end TEXT,
            activation_date TEXT,
            last_check_date TEXT,
            next_check_date TEXT,
            grace_period_days INTEGER DEFAULT 7,
            trial_end_date TEXT,
            cancel_at_period_end INTEGER DEFAULT 0,
            canceled_at TEXT,
            payment_provider TEXT,
            payment_id TEXT,
            stripe_subscription_id TEXT,
            paypal_subscription_id TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id),
            FOREIGN KEY (customer_id) REFERENCES customers(id),
            FOREIGN KEY (plan_id) REFERENCES billing_plans(id)
        )
        ''',

        # ==================== Invoices ====================
        '''
        CREATE TABLE IF NOT EXISTS invoices (
            id TEXT PRIMARY KEY,
            invoice_number TEXT NOT NULL UNIQUE,
            customer_id TEXT NOT NULL,
            subscription_id TEXT,
            license_id TEXT,
            status TEXT DEFAULT 'draft',
            currency TEXT DEFAULT 'USD',
            subtotal REAL DEFAULT 0,
            tax_amount REAL DEFAULT 0,
            tax_rate REAL DEFAULT 0,
            discount_amount REAL DEFAULT 0,
            discount_code TEXT,
            total REAL DEFAULT 0,
            amount_paid REAL DEFAULT 0,
            amount_due REAL DEFAULT 0,
            issue_date TEXT NOT NULL,
            due_date TEXT,
            paid_at TEXT,
            payment_method_id TEXT,
            payment_provider TEXT,
            payment_provider_id TEXT,
            notes TEXT,
            pdf_path TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id),
            FOREIGN KEY (subscription_id) REFERENCES subscriptions(id),
            FOREIGN KEY (license_id) REFERENCES licenses(id),
            FOREIGN KEY (payment_method_id) REFERENCES payment_methods(id)
        )
        ''',

        # ==================== Invoice Line Items ====================
        '''
        CREATE TABLE IF NOT EXISTS invoice_items (
            id TEXT PRIMARY KEY,
            invoice_id TEXT NOT NULL,
            description TEXT NOT NULL,
            quantity INTEGER DEFAULT 1,
            unit_price REAL NOT NULL,
            amount REAL NOT NULL,
            plan_id TEXT,
            license_id TEXT,
            period_start TEXT,
            period_end TEXT,
            created_at TEXT NOT NULL,
            FOREIGN KEY (invoice_id) REFERENCES invoices(id),
            FOREIGN KEY (plan_id) REFERENCES billing_plans(id),
            FOREIGN KEY (license_id) REFERENCES licenses(id)
        )
        ''',

        # ==================== Billing History (Transactions) ====================
        '''
        CREATE TABLE IF NOT EXISTS billing_history (
            id TEXT PRIMARY KEY,
            customer_id TEXT NOT NULL,
            invoice_id TEXT,
            subscription_id TEXT,
            type TEXT NOT NULL,
            status TEXT NOT NULL,
            amount REAL NOT NULL,
            currency TEXT DEFAULT 'USD',
            payment_method_id TEXT,
            payment_provider TEXT,
            payment_provider_id TEXT,
            failure_reason TEXT,
            refund_reason TEXT,
            refunded_amount REAL DEFAULT 0,
            metadata TEXT,
            created_at TEXT NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id),
            FOREIGN KEY (invoice_id) REFERENCES invoices(id),
            FOREIGN KEY (subscription_id) REFERENCES subscriptions(id),
            FOREIGN KEY (payment_method_id) REFERENCES payment_methods(id)
        )
        ''',

        # ==================== Discount Codes ====================
        '''
        CREATE TABLE IF NOT EXISTS discount_codes (
            id TEXT PRIMARY KEY,
            code TEXT NOT NULL UNIQUE,
            description TEXT,
            discount_type TEXT NOT NULL,
            discount_value REAL NOT NULL,
            currency TEXT DEFAULT 'USD',
            max_uses INTEGER,
            times_used INTEGER DEFAULT 0,
            min_amount REAL DEFAULT 0,
            applies_to TEXT,
            plan_ids TEXT,
            valid_from TEXT,
            valid_until TEXT,
            is_active INTEGER DEFAULT 1,
            created_by TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
        ''',

        # ==================== Activation Logs ====================
        '''
        CREATE TABLE IF NOT EXISTS activation_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            license_id TEXT NOT NULL,
            customer_id TEXT,
            action TEXT NOT NULL,
            hardware_fingerprint TEXT,
            ip_address TEXT,
            user_agent TEXT,
            success INTEGER DEFAULT 1,
            error_message TEXT,
            created_at TEXT NOT NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id),
            FOREIGN KEY (customer_id) REFERENCES customers(id)
        )
        ''',

        # ==================== Used Nonces ====================
        '''
        CREATE TABLE IF NOT EXISTS used_nonces (
            nonce TEXT PRIMARY KEY,
            license_id TEXT NOT NULL,
            used_at TEXT NOT NULL
        )
        ''',

        # ==================== Admin Audit Log ====================
        '''
        CREATE TABLE IF NOT EXISTS admin_audit_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            admin_user_id TEXT,
            admin_user TEXT NOT NULL,
            action TEXT NOT NULL,
            target_type TEXT,
            target_id TEXT,
            details TEXT,
            ip_address TEXT,
            user_agent TEXT,
            created_at TEXT NOT NULL,
            FOREIGN KEY (admin_user_id) REFERENCES admin_users(id)
        )
        ''',

        # ==================== License Seats ====================
        '''
        CREATE TABLE IF NOT EXISTS license_seats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            license_id TEXT NOT NULL,
            customer_id TEXT,
            hardware_fingerprint TEXT NOT NULL,
            device_name TEXT,
            os_info TEXT,
            browser_version TEXT,
            first_activated_at TEXT NOT NULL,
            last_seen_at TEXT NOT NULL,
            ip_address TEXT,
            user_agent TEXT,
            is_active INTEGER DEFAULT 1,
            deactivated_at TEXT,
            deactivated_reason TEXT,
            UNIQUE(license_id, hardware_fingerprint),
            FOREIGN KEY (license_id) REFERENCES licenses(id),
            FOREIGN KEY (customer_id) REFERENCES customers(id)
        )
        ''',

        # ==================== Customer Activity Log ====================
        '''
        CREATE TABLE IF NOT EXISTS customer_activity_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            customer_id TEXT NOT NULL,
            license_id TEXT,
            activity_type TEXT NOT NULL,
            description TEXT,
            ip_address TEXT,
            user_agent TEXT,
            metadata TEXT,
            created_at TEXT NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id),
            FOREIGN KEY (license_id) REFERENCES licenses(id)
        )
        ''',

        # ==================== Email Templates ====================
        '''
        CREATE TABLE IF NOT EXISTS email_templates (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            subject TEXT NOT NULL,
            body_html TEXT NOT NULL,
            body_text TEXT,
            variables TEXT,
            is_active INTEGER DEFAULT 1,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
        ''',

        # ==================== Email Log ====================
        '''
        CREATE TABLE IF NOT EXISTS email_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            customer_id TEXT,
            template_id TEXT,
            to_email TEXT NOT NULL,
            subject TEXT NOT NULL,
            status TEXT DEFAULT 'pending',
            provider TEXT,
            provider_message_id TEXT,
            error_message TEXT,
            sent_at TEXT,
            opened_at TEXT,
            clicked_at TEXT,
            created_at TEXT NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id),
            FOREIGN KEY (template_id) REFERENCES email_templates(id)
        )
        ''',

        # ==================== Settings ====================
        '''
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT,
            type TEXT DEFAULT 'string',
            description TEXT,
            updated_at TEXT NOT NULL
        )
        ''',

        # ==================== Invoice Reminders (Scheduler Tracking) ====================
        '''
        CREATE TABLE IF NOT EXISTS invoice_reminders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            invoice_id TEXT NOT NULL,
            days_before INTEGER NOT NULL,
            sent_at TEXT NOT NULL,
            FOREIGN KEY (invoice_id) REFERENCES invoices(id)
        )
        '''
    ]


# ============================================================================
# PostgreSQL Schema
# ============================================================================

def get_postgresql_schema() -> list:
    """Get PostgreSQL-compatible CREATE TABLE statements."""
    return [
        # Admin Users
        '''
        CREATE TABLE IF NOT EXISTS admin_users (
            id VARCHAR(36) PRIMARY KEY,
            username VARCHAR(100) NOT NULL UNIQUE,
            email VARCHAR(255) NOT NULL UNIQUE,
            password_hash VARCHAR(255) NOT NULL,
            role VARCHAR(50) DEFAULT 'admin',
            is_active BOOLEAN DEFAULT TRUE,
            last_login_at TIMESTAMP,
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Billing Plans
        '''
        CREATE TABLE IF NOT EXISTS billing_plans (
            id VARCHAR(36) PRIMARY KEY,
            name VARCHAR(100) NOT NULL UNIQUE,
            display_name VARCHAR(200) NOT NULL,
            description TEXT,
            license_type INTEGER NOT NULL,
            price_monthly DECIMAL(10,2) DEFAULT 0,
            price_yearly DECIMAL(10,2) DEFAULT 0,
            price_lifetime DECIMAL(10,2) DEFAULT 0,
            currency VARCHAR(3) DEFAULT 'USD',
            max_seats INTEGER DEFAULT 1,
            features JSONB,
            is_active BOOLEAN DEFAULT TRUE,
            sort_order INTEGER DEFAULT 0,
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Customers
        '''
        CREATE TABLE IF NOT EXISTS customers (
            id VARCHAR(36) PRIMARY KEY,
            email VARCHAR(255) NOT NULL UNIQUE,
            name VARCHAR(255) NOT NULL,
            company VARCHAR(255),
            phone VARCHAR(50),
            address_line1 VARCHAR(255),
            address_line2 VARCHAR(255),
            city VARCHAR(100),
            state VARCHAR(100),
            postal_code VARCHAR(20),
            country VARCHAR(100) DEFAULT 'US',
            tax_id VARCHAR(50),
            stripe_customer_id VARCHAR(100),
            paypal_customer_id VARCHAR(100),
            notes TEXT,
            status VARCHAR(50) DEFAULT 'active',
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Licenses (must come before customer_users which references it)
        '''
        CREATE TABLE IF NOT EXISTS licenses (
            id VARCHAR(36) PRIMARY KEY,
            customer_id VARCHAR(36) REFERENCES customers(id),
            plan_id VARCHAR(36) REFERENCES billing_plans(id),
            license_type INTEGER NOT NULL,
            name VARCHAR(255) NOT NULL,
            email VARCHAR(255) NOT NULL,
            organization VARCHAR(255),
            max_seats INTEGER DEFAULT 1,
            issue_date TIMESTAMP NOT NULL,
            expiry_date TIMESTAMP,
            hardware_bound BOOLEAN DEFAULT FALSE,
            hardware_fingerprint VARCHAR(255),
            feature_flags INTEGER DEFAULT 0,
            custom_data JSONB,
            issuer VARCHAR(255),
            notes TEXT,
            file_path VARCHAR(512),
            status VARCHAR(50) DEFAULT 'active',
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL,
            -- Version 2 Extended Metadata
            license_version INTEGER DEFAULT 2,
            min_browser_version VARCHAR(50),
            max_browser_version VARCHAR(50),
            allowed_regions TEXT,
            export_control VARCHAR(100),
            total_activations INTEGER DEFAULT 0,
            last_device_name VARCHAR(255),
            order_id VARCHAR(100),
            invoice_id VARCHAR(100),
            reseller_id VARCHAR(100),
            support_tier INTEGER DEFAULT 0,
            support_expiry_date TIMESTAMP,
            revocation_check_url TEXT,
            issued_ip VARCHAR(45),
            maintenance_included BOOLEAN DEFAULT FALSE,
            maintenance_expiry_date TIMESTAMP
        )
        ''',

        # Customer Users (Portal Login)
        '''
        CREATE TABLE IF NOT EXISTS customer_users (
            id VARCHAR(36) PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL UNIQUE REFERENCES licenses(id),
            email VARCHAR(255) NOT NULL,
            password_hash VARCHAR(255) NOT NULL,
            customer_id VARCHAR(36) REFERENCES customers(id),
            is_active BOOLEAN DEFAULT TRUE,
            email_verified BOOLEAN DEFAULT FALSE,
            email_verification_token VARCHAR(100),
            email_verification_expires TIMESTAMP,
            password_reset_token VARCHAR(100),
            password_reset_expires TIMESTAMP,
            last_login_at TIMESTAMP,
            last_login_ip VARCHAR(45),
            login_count INTEGER DEFAULT 0,
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Customer Sessions
        '''
        CREATE TABLE IF NOT EXISTS customer_sessions (
            id VARCHAR(36) PRIMARY KEY,
            user_id VARCHAR(36) NOT NULL REFERENCES customer_users(id),
            token_hash VARCHAR(255) NOT NULL,
            ip_address VARCHAR(45),
            user_agent TEXT,
            expires_at TIMESTAMP NOT NULL,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # Payment Methods
        '''
        CREATE TABLE IF NOT EXISTS payment_methods (
            id VARCHAR(36) PRIMARY KEY,
            customer_id VARCHAR(36) NOT NULL REFERENCES customers(id),
            type VARCHAR(50) NOT NULL,
            provider VARCHAR(50) NOT NULL,
            provider_payment_id VARCHAR(255),
            last_four VARCHAR(4),
            brand VARCHAR(50),
            exp_month INTEGER,
            exp_year INTEGER,
            is_default BOOLEAN DEFAULT FALSE,
            is_active BOOLEAN DEFAULT TRUE,
            billing_name VARCHAR(255),
            billing_email VARCHAR(255),
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Subscriptions
        '''
        CREATE TABLE IF NOT EXISTS subscriptions (
            id VARCHAR(36) PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL UNIQUE REFERENCES licenses(id),
            customer_id VARCHAR(36) REFERENCES customers(id),
            plan_id VARCHAR(36) REFERENCES billing_plans(id),
            status VARCHAR(50) DEFAULT 'active',
            billing_cycle VARCHAR(20) DEFAULT 'monthly',
            current_period_start TIMESTAMP,
            current_period_end TIMESTAMP,
            activation_date TIMESTAMP,
            last_check_date TIMESTAMP,
            next_check_date TIMESTAMP,
            grace_period_days INTEGER DEFAULT 7,
            trial_end_date TIMESTAMP,
            cancel_at_period_end BOOLEAN DEFAULT FALSE,
            canceled_at TIMESTAMP,
            payment_provider VARCHAR(50),
            payment_id VARCHAR(255),
            stripe_subscription_id VARCHAR(100),
            paypal_subscription_id VARCHAR(100),
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Invoices
        '''
        CREATE TABLE IF NOT EXISTS invoices (
            id VARCHAR(36) PRIMARY KEY,
            invoice_number VARCHAR(50) NOT NULL UNIQUE,
            customer_id VARCHAR(36) NOT NULL REFERENCES customers(id),
            subscription_id VARCHAR(36) REFERENCES subscriptions(id),
            license_id VARCHAR(36) REFERENCES licenses(id),
            status VARCHAR(50) DEFAULT 'draft',
            currency VARCHAR(3) DEFAULT 'USD',
            subtotal DECIMAL(10,2) DEFAULT 0,
            tax_amount DECIMAL(10,2) DEFAULT 0,
            tax_rate DECIMAL(5,2) DEFAULT 0,
            discount_amount DECIMAL(10,2) DEFAULT 0,
            discount_code VARCHAR(50),
            total DECIMAL(10,2) DEFAULT 0,
            amount_paid DECIMAL(10,2) DEFAULT 0,
            amount_due DECIMAL(10,2) DEFAULT 0,
            issue_date TIMESTAMP NOT NULL,
            due_date TIMESTAMP,
            paid_at TIMESTAMP,
            payment_method_id VARCHAR(36) REFERENCES payment_methods(id),
            payment_provider VARCHAR(50),
            payment_provider_id VARCHAR(255),
            notes TEXT,
            pdf_path VARCHAR(512),
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Invoice Line Items
        '''
        CREATE TABLE IF NOT EXISTS invoice_items (
            id VARCHAR(36) PRIMARY KEY,
            invoice_id VARCHAR(36) NOT NULL REFERENCES invoices(id),
            description TEXT NOT NULL,
            quantity INTEGER DEFAULT 1,
            unit_price DECIMAL(10,2) NOT NULL,
            amount DECIMAL(10,2) NOT NULL,
            plan_id VARCHAR(36) REFERENCES billing_plans(id),
            license_id VARCHAR(36) REFERENCES licenses(id),
            period_start TIMESTAMP,
            period_end TIMESTAMP,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # Billing History
        '''
        CREATE TABLE IF NOT EXISTS billing_history (
            id VARCHAR(36) PRIMARY KEY,
            customer_id VARCHAR(36) NOT NULL REFERENCES customers(id),
            invoice_id VARCHAR(36) REFERENCES invoices(id),
            subscription_id VARCHAR(36) REFERENCES subscriptions(id),
            type VARCHAR(50) NOT NULL,
            status VARCHAR(50) NOT NULL,
            amount DECIMAL(10,2) NOT NULL,
            currency VARCHAR(3) DEFAULT 'USD',
            payment_method_id VARCHAR(36) REFERENCES payment_methods(id),
            payment_provider VARCHAR(50),
            payment_provider_id VARCHAR(255),
            failure_reason TEXT,
            refund_reason TEXT,
            refunded_amount DECIMAL(10,2) DEFAULT 0,
            metadata JSONB,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # Discount Codes
        '''
        CREATE TABLE IF NOT EXISTS discount_codes (
            id VARCHAR(36) PRIMARY KEY,
            code VARCHAR(50) NOT NULL UNIQUE,
            description TEXT,
            discount_type VARCHAR(20) NOT NULL,
            discount_value DECIMAL(10,2) NOT NULL,
            currency VARCHAR(3) DEFAULT 'USD',
            max_uses INTEGER,
            times_used INTEGER DEFAULT 0,
            min_amount DECIMAL(10,2) DEFAULT 0,
            applies_to VARCHAR(50),
            plan_ids TEXT,
            valid_from TIMESTAMP,
            valid_until TIMESTAMP,
            is_active BOOLEAN DEFAULT TRUE,
            created_by VARCHAR(36),
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Activation Logs
        '''
        CREATE TABLE IF NOT EXISTS activation_logs (
            id SERIAL PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL REFERENCES licenses(id),
            customer_id VARCHAR(36) REFERENCES customers(id),
            action VARCHAR(100) NOT NULL,
            hardware_fingerprint VARCHAR(255),
            ip_address VARCHAR(45),
            user_agent TEXT,
            success BOOLEAN DEFAULT TRUE,
            error_message TEXT,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # Used Nonces
        '''
        CREATE TABLE IF NOT EXISTS used_nonces (
            nonce VARCHAR(255) PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL,
            used_at TIMESTAMP NOT NULL
        )
        ''',

        # Admin Audit Log
        '''
        CREATE TABLE IF NOT EXISTS admin_audit_log (
            id SERIAL PRIMARY KEY,
            admin_user_id VARCHAR(36) REFERENCES admin_users(id),
            admin_user VARCHAR(255) NOT NULL,
            action VARCHAR(100) NOT NULL,
            target_type VARCHAR(50),
            target_id VARCHAR(36),
            details TEXT,
            ip_address VARCHAR(45),
            user_agent TEXT,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # License Seats
        '''
        CREATE TABLE IF NOT EXISTS license_seats (
            id SERIAL PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL REFERENCES licenses(id),
            customer_id VARCHAR(36) REFERENCES customers(id),
            hardware_fingerprint VARCHAR(255) NOT NULL,
            device_name VARCHAR(255),
            os_info VARCHAR(255),
            browser_version VARCHAR(50),
            first_activated_at TIMESTAMP NOT NULL,
            last_seen_at TIMESTAMP NOT NULL,
            ip_address VARCHAR(45),
            user_agent TEXT,
            is_active BOOLEAN DEFAULT TRUE,
            deactivated_at TIMESTAMP,
            deactivated_reason TEXT,
            UNIQUE(license_id, hardware_fingerprint)
        )
        ''',

        # Customer Activity Log
        '''
        CREATE TABLE IF NOT EXISTS customer_activity_log (
            id SERIAL PRIMARY KEY,
            customer_id VARCHAR(36) NOT NULL REFERENCES customers(id),
            license_id VARCHAR(36) REFERENCES licenses(id),
            activity_type VARCHAR(100) NOT NULL,
            description TEXT,
            ip_address VARCHAR(45),
            user_agent TEXT,
            metadata JSONB,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # Email Templates
        '''
        CREATE TABLE IF NOT EXISTS email_templates (
            id VARCHAR(36) PRIMARY KEY,
            name VARCHAR(100) NOT NULL UNIQUE,
            subject VARCHAR(255) NOT NULL,
            body_html TEXT NOT NULL,
            body_text TEXT,
            variables TEXT,
            is_active BOOLEAN DEFAULT TRUE,
            created_at TIMESTAMP NOT NULL,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Email Log
        '''
        CREATE TABLE IF NOT EXISTS email_log (
            id SERIAL PRIMARY KEY,
            customer_id VARCHAR(36) REFERENCES customers(id),
            template_id VARCHAR(36) REFERENCES email_templates(id),
            to_email VARCHAR(255) NOT NULL,
            subject VARCHAR(255) NOT NULL,
            status VARCHAR(50) DEFAULT 'pending',
            provider VARCHAR(50),
            provider_message_id VARCHAR(255),
            error_message TEXT,
            sent_at TIMESTAMP,
            opened_at TIMESTAMP,
            clicked_at TIMESTAMP,
            created_at TIMESTAMP NOT NULL
        )
        ''',

        # Settings
        '''
        CREATE TABLE IF NOT EXISTS settings (
            key VARCHAR(100) PRIMARY KEY,
            value TEXT,
            type VARCHAR(50) DEFAULT 'string',
            description TEXT,
            updated_at TIMESTAMP NOT NULL
        )
        ''',

        # Invoice Reminders (Scheduler Tracking)
        '''
        CREATE TABLE IF NOT EXISTS invoice_reminders (
            id SERIAL PRIMARY KEY,
            invoice_id VARCHAR(36) NOT NULL REFERENCES invoices(id),
            days_before INTEGER NOT NULL,
            sent_at TIMESTAMP NOT NULL
        )
        '''
    ]


# ============================================================================
# MySQL Schema
# ============================================================================

def get_mysql_schema() -> list:
    """Get MySQL-compatible CREATE TABLE statements."""
    return [
        # Admin Users
        '''
        CREATE TABLE IF NOT EXISTS admin_users (
            id VARCHAR(36) PRIMARY KEY,
            username VARCHAR(100) NOT NULL UNIQUE,
            email VARCHAR(255) NOT NULL UNIQUE,
            password_hash VARCHAR(255) NOT NULL,
            role VARCHAR(50) DEFAULT 'admin',
            is_active TINYINT(1) DEFAULT 1,
            last_login_at DATETIME,
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Billing Plans
        '''
        CREATE TABLE IF NOT EXISTS billing_plans (
            id VARCHAR(36) PRIMARY KEY,
            name VARCHAR(100) NOT NULL UNIQUE,
            display_name VARCHAR(200) NOT NULL,
            description TEXT,
            license_type INT NOT NULL,
            price_monthly DECIMAL(10,2) DEFAULT 0,
            price_yearly DECIMAL(10,2) DEFAULT 0,
            price_lifetime DECIMAL(10,2) DEFAULT 0,
            currency VARCHAR(3) DEFAULT 'USD',
            max_seats INT DEFAULT 1,
            features JSON,
            is_active TINYINT(1) DEFAULT 1,
            sort_order INT DEFAULT 0,
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Customers
        '''
        CREATE TABLE IF NOT EXISTS customers (
            id VARCHAR(36) PRIMARY KEY,
            email VARCHAR(255) NOT NULL UNIQUE,
            name VARCHAR(255) NOT NULL,
            company VARCHAR(255),
            phone VARCHAR(50),
            address_line1 VARCHAR(255),
            address_line2 VARCHAR(255),
            city VARCHAR(100),
            state VARCHAR(100),
            postal_code VARCHAR(20),
            country VARCHAR(100) DEFAULT 'US',
            tax_id VARCHAR(50),
            stripe_customer_id VARCHAR(100),
            paypal_customer_id VARCHAR(100),
            notes TEXT,
            status VARCHAR(50) DEFAULT 'active',
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Customer Users (Portal Login)
        '''
        CREATE TABLE IF NOT EXISTS customer_users (
            id VARCHAR(36) PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL UNIQUE,
            email VARCHAR(255) NOT NULL,
            password_hash VARCHAR(255) NOT NULL,
            customer_id VARCHAR(36),
            is_active TINYINT(1) DEFAULT 1,
            email_verified TINYINT(1) DEFAULT 0,
            email_verification_token VARCHAR(100),
            email_verification_expires DATETIME,
            password_reset_token VARCHAR(100),
            password_reset_expires DATETIME,
            last_login_at DATETIME,
            last_login_ip VARCHAR(45),
            login_count INT DEFAULT 0,
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE CASCADE,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Customer Sessions
        '''
        CREATE TABLE IF NOT EXISTS customer_sessions (
            id VARCHAR(36) PRIMARY KEY,
            user_id VARCHAR(36) NOT NULL,
            token_hash VARCHAR(255) NOT NULL,
            ip_address VARCHAR(45),
            user_agent TEXT,
            expires_at DATETIME NOT NULL,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (user_id) REFERENCES customer_users(id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Payment Methods
        '''
        CREATE TABLE IF NOT EXISTS payment_methods (
            id VARCHAR(36) PRIMARY KEY,
            customer_id VARCHAR(36) NOT NULL,
            type VARCHAR(50) NOT NULL,
            provider VARCHAR(50) NOT NULL,
            provider_payment_id VARCHAR(255),
            last_four VARCHAR(4),
            brand VARCHAR(50),
            exp_month INT,
            exp_year INT,
            is_default TINYINT(1) DEFAULT 0,
            is_active TINYINT(1) DEFAULT 1,
            billing_name VARCHAR(255),
            billing_email VARCHAR(255),
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Licenses
        '''
        CREATE TABLE IF NOT EXISTS licenses (
            id VARCHAR(36) PRIMARY KEY,
            customer_id VARCHAR(36),
            plan_id VARCHAR(36),
            license_type INT NOT NULL,
            name VARCHAR(255) NOT NULL,
            email VARCHAR(255) NOT NULL,
            organization VARCHAR(255),
            max_seats INT DEFAULT 1,
            issue_date DATETIME NOT NULL,
            expiry_date DATETIME,
            hardware_bound TINYINT(1) DEFAULT 0,
            hardware_fingerprint VARCHAR(255),
            feature_flags INT DEFAULT 0,
            custom_data JSON,
            issuer VARCHAR(255),
            notes TEXT,
            file_path VARCHAR(512),
            status VARCHAR(50) DEFAULT 'active',
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL,
            -- Version 2 Extended Metadata
            license_version INT DEFAULT 2,
            min_browser_version VARCHAR(50),
            max_browser_version VARCHAR(50),
            allowed_regions TEXT,
            export_control VARCHAR(100),
            total_activations INT DEFAULT 0,
            last_device_name VARCHAR(255),
            order_id VARCHAR(100),
            invoice_id VARCHAR(100),
            reseller_id VARCHAR(100),
            support_tier INT DEFAULT 0,
            support_expiry_date DATETIME,
            revocation_check_url TEXT,
            issued_ip VARCHAR(45),
            maintenance_included TINYINT(1) DEFAULT 0,
            maintenance_expiry_date DATETIME,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL,
            FOREIGN KEY (plan_id) REFERENCES billing_plans(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Subscriptions
        '''
        CREATE TABLE IF NOT EXISTS subscriptions (
            id VARCHAR(36) PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL UNIQUE,
            customer_id VARCHAR(36),
            plan_id VARCHAR(36),
            status VARCHAR(50) DEFAULT 'active',
            billing_cycle VARCHAR(20) DEFAULT 'monthly',
            current_period_start DATETIME,
            current_period_end DATETIME,
            activation_date DATETIME,
            last_check_date DATETIME,
            next_check_date DATETIME,
            grace_period_days INT DEFAULT 7,
            trial_end_date DATETIME,
            cancel_at_period_end TINYINT(1) DEFAULT 0,
            canceled_at DATETIME,
            payment_provider VARCHAR(50),
            payment_id VARCHAR(255),
            stripe_subscription_id VARCHAR(100),
            paypal_subscription_id VARCHAR(100),
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE CASCADE,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL,
            FOREIGN KEY (plan_id) REFERENCES billing_plans(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Invoices
        '''
        CREATE TABLE IF NOT EXISTS invoices (
            id VARCHAR(36) PRIMARY KEY,
            invoice_number VARCHAR(50) NOT NULL UNIQUE,
            customer_id VARCHAR(36) NOT NULL,
            subscription_id VARCHAR(36),
            license_id VARCHAR(36),
            status VARCHAR(50) DEFAULT 'draft',
            currency VARCHAR(3) DEFAULT 'USD',
            subtotal DECIMAL(10,2) DEFAULT 0,
            tax_amount DECIMAL(10,2) DEFAULT 0,
            tax_rate DECIMAL(5,2) DEFAULT 0,
            discount_amount DECIMAL(10,2) DEFAULT 0,
            discount_code VARCHAR(50),
            total DECIMAL(10,2) DEFAULT 0,
            amount_paid DECIMAL(10,2) DEFAULT 0,
            amount_due DECIMAL(10,2) DEFAULT 0,
            issue_date DATETIME NOT NULL,
            due_date DATETIME,
            paid_at DATETIME,
            payment_method_id VARCHAR(36),
            payment_provider VARCHAR(50),
            payment_provider_id VARCHAR(255),
            notes TEXT,
            pdf_path VARCHAR(512),
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE,
            FOREIGN KEY (subscription_id) REFERENCES subscriptions(id) ON DELETE SET NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE SET NULL,
            FOREIGN KEY (payment_method_id) REFERENCES payment_methods(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Invoice Line Items
        '''
        CREATE TABLE IF NOT EXISTS invoice_items (
            id VARCHAR(36) PRIMARY KEY,
            invoice_id VARCHAR(36) NOT NULL,
            description TEXT NOT NULL,
            quantity INT DEFAULT 1,
            unit_price DECIMAL(10,2) NOT NULL,
            amount DECIMAL(10,2) NOT NULL,
            plan_id VARCHAR(36),
            license_id VARCHAR(36),
            period_start DATETIME,
            period_end DATETIME,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (invoice_id) REFERENCES invoices(id) ON DELETE CASCADE,
            FOREIGN KEY (plan_id) REFERENCES billing_plans(id) ON DELETE SET NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Billing History
        '''
        CREATE TABLE IF NOT EXISTS billing_history (
            id VARCHAR(36) PRIMARY KEY,
            customer_id VARCHAR(36) NOT NULL,
            invoice_id VARCHAR(36),
            subscription_id VARCHAR(36),
            type VARCHAR(50) NOT NULL,
            status VARCHAR(50) NOT NULL,
            amount DECIMAL(10,2) NOT NULL,
            currency VARCHAR(3) DEFAULT 'USD',
            payment_method_id VARCHAR(36),
            payment_provider VARCHAR(50),
            payment_provider_id VARCHAR(255),
            failure_reason TEXT,
            refund_reason TEXT,
            refunded_amount DECIMAL(10,2) DEFAULT 0,
            metadata JSON,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE,
            FOREIGN KEY (invoice_id) REFERENCES invoices(id) ON DELETE SET NULL,
            FOREIGN KEY (subscription_id) REFERENCES subscriptions(id) ON DELETE SET NULL,
            FOREIGN KEY (payment_method_id) REFERENCES payment_methods(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Discount Codes
        '''
        CREATE TABLE IF NOT EXISTS discount_codes (
            id VARCHAR(36) PRIMARY KEY,
            code VARCHAR(50) NOT NULL UNIQUE,
            description TEXT,
            discount_type VARCHAR(20) NOT NULL,
            discount_value DECIMAL(10,2) NOT NULL,
            currency VARCHAR(3) DEFAULT 'USD',
            max_uses INT,
            times_used INT DEFAULT 0,
            min_amount DECIMAL(10,2) DEFAULT 0,
            applies_to VARCHAR(50),
            plan_ids TEXT,
            valid_from DATETIME,
            valid_until DATETIME,
            is_active TINYINT(1) DEFAULT 1,
            created_by VARCHAR(36),
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Activation Logs
        '''
        CREATE TABLE IF NOT EXISTS activation_logs (
            id INT AUTO_INCREMENT PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL,
            customer_id VARCHAR(36),
            action VARCHAR(100) NOT NULL,
            hardware_fingerprint VARCHAR(255),
            ip_address VARCHAR(45),
            user_agent TEXT,
            success TINYINT(1) DEFAULT 1,
            error_message TEXT,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE CASCADE,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Used Nonces
        '''
        CREATE TABLE IF NOT EXISTS used_nonces (
            nonce VARCHAR(255) PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL,
            used_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Admin Audit Log
        '''
        CREATE TABLE IF NOT EXISTS admin_audit_log (
            id INT AUTO_INCREMENT PRIMARY KEY,
            admin_user_id VARCHAR(36),
            admin_user VARCHAR(255) NOT NULL,
            action VARCHAR(100) NOT NULL,
            target_type VARCHAR(50),
            target_id VARCHAR(36),
            details TEXT,
            ip_address VARCHAR(45),
            user_agent TEXT,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (admin_user_id) REFERENCES admin_users(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # License Seats
        '''
        CREATE TABLE IF NOT EXISTS license_seats (
            id INT AUTO_INCREMENT PRIMARY KEY,
            license_id VARCHAR(36) NOT NULL,
            customer_id VARCHAR(36),
            hardware_fingerprint VARCHAR(255) NOT NULL,
            device_name VARCHAR(255),
            os_info VARCHAR(255),
            browser_version VARCHAR(50),
            first_activated_at DATETIME NOT NULL,
            last_seen_at DATETIME NOT NULL,
            ip_address VARCHAR(45),
            user_agent TEXT,
            is_active TINYINT(1) DEFAULT 1,
            deactivated_at DATETIME,
            deactivated_reason TEXT,
            UNIQUE KEY unique_seat (license_id, hardware_fingerprint),
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE CASCADE,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Customer Activity Log
        '''
        CREATE TABLE IF NOT EXISTS customer_activity_log (
            id INT AUTO_INCREMENT PRIMARY KEY,
            customer_id VARCHAR(36) NOT NULL,
            license_id VARCHAR(36),
            activity_type VARCHAR(100) NOT NULL,
            description TEXT,
            ip_address VARCHAR(45),
            user_agent TEXT,
            metadata JSON,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE,
            FOREIGN KEY (license_id) REFERENCES licenses(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Email Templates
        '''
        CREATE TABLE IF NOT EXISTS email_templates (
            id VARCHAR(36) PRIMARY KEY,
            name VARCHAR(100) NOT NULL UNIQUE,
            subject VARCHAR(255) NOT NULL,
            body_html TEXT NOT NULL,
            body_text TEXT,
            variables TEXT,
            is_active TINYINT(1) DEFAULT 1,
            created_at DATETIME NOT NULL,
            updated_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Email Log
        '''
        CREATE TABLE IF NOT EXISTS email_log (
            id INT AUTO_INCREMENT PRIMARY KEY,
            customer_id VARCHAR(36),
            template_id VARCHAR(36),
            to_email VARCHAR(255) NOT NULL,
            subject VARCHAR(255) NOT NULL,
            status VARCHAR(50) DEFAULT 'pending',
            provider VARCHAR(50),
            provider_message_id VARCHAR(255),
            error_message TEXT,
            sent_at DATETIME,
            opened_at DATETIME,
            clicked_at DATETIME,
            created_at DATETIME NOT NULL,
            FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL,
            FOREIGN KEY (template_id) REFERENCES email_templates(id) ON DELETE SET NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Settings
        '''
        CREATE TABLE IF NOT EXISTS settings (
            `key` VARCHAR(100) PRIMARY KEY,
            value TEXT,
            type VARCHAR(50) DEFAULT 'string',
            description TEXT,
            updated_at DATETIME NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        ''',

        # Invoice Reminders (Scheduler Tracking)
        '''
        CREATE TABLE IF NOT EXISTS invoice_reminders (
            id INT AUTO_INCREMENT PRIMARY KEY,
            invoice_id VARCHAR(36) NOT NULL,
            days_before INT NOT NULL,
            sent_at DATETIME NOT NULL,
            FOREIGN KEY (invoice_id) REFERENCES invoices(id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        '''
    ]


# ============================================================================
# Indexes
# ============================================================================

def get_index_statements(db_type: str) -> list:
    """Get index creation statements for the specified database type."""
    return [
        # Customer indexes
        'CREATE INDEX IF NOT EXISTS idx_customers_email ON customers(email)',
        'CREATE INDEX IF NOT EXISTS idx_customers_status ON customers(status)',
        'CREATE INDEX IF NOT EXISTS idx_customers_stripe ON customers(stripe_customer_id)',

        # License indexes
        'CREATE INDEX IF NOT EXISTS idx_licenses_email ON licenses(email)',
        'CREATE INDEX IF NOT EXISTS idx_licenses_status ON licenses(status)',
        'CREATE INDEX IF NOT EXISTS idx_licenses_customer ON licenses(customer_id)',
        'CREATE INDEX IF NOT EXISTS idx_licenses_plan ON licenses(plan_id)',

        # Subscription indexes
        'CREATE INDEX IF NOT EXISTS idx_subscriptions_status ON subscriptions(status)',
        'CREATE INDEX IF NOT EXISTS idx_subscriptions_customer ON subscriptions(customer_id)',
        'CREATE INDEX IF NOT EXISTS idx_subscriptions_plan ON subscriptions(plan_id)',

        # Invoice indexes
        'CREATE INDEX IF NOT EXISTS idx_invoices_customer ON invoices(customer_id)',
        'CREATE INDEX IF NOT EXISTS idx_invoices_status ON invoices(status)',
        'CREATE INDEX IF NOT EXISTS idx_invoices_date ON invoices(issue_date)',

        # Billing history indexes
        'CREATE INDEX IF NOT EXISTS idx_billing_history_customer ON billing_history(customer_id)',
        'CREATE INDEX IF NOT EXISTS idx_billing_history_date ON billing_history(created_at)',

        # Activation log indexes
        'CREATE INDEX IF NOT EXISTS idx_activation_logs_license ON activation_logs(license_id)',
        'CREATE INDEX IF NOT EXISTS idx_activation_logs_customer ON activation_logs(customer_id)',

        # Seat indexes
        'CREATE INDEX IF NOT EXISTS idx_license_seats_license ON license_seats(license_id)',
        'CREATE INDEX IF NOT EXISTS idx_license_seats_hardware ON license_seats(hardware_fingerprint)',
        'CREATE INDEX IF NOT EXISTS idx_license_seats_customer ON license_seats(customer_id)',

        # Activity log indexes
        'CREATE INDEX IF NOT EXISTS idx_customer_activity_customer ON customer_activity_log(customer_id)',
        'CREATE INDEX IF NOT EXISTS idx_customer_activity_date ON customer_activity_log(created_at)',

        # Admin audit indexes
        'CREATE INDEX IF NOT EXISTS idx_admin_audit_user ON admin_audit_log(admin_user_id)',
        'CREATE INDEX IF NOT EXISTS idx_admin_audit_date ON admin_audit_log(created_at)',

        # Email log indexes
        'CREATE INDEX IF NOT EXISTS idx_email_log_customer ON email_log(customer_id)',
        'CREATE INDEX IF NOT EXISTS idx_email_log_status ON email_log(status)',
    ]


def get_drop_statements() -> list:
    """Get DROP TABLE statements in correct order (respecting foreign keys)."""
    return [
        'DROP TABLE IF EXISTS email_log',
        'DROP TABLE IF EXISTS email_templates',
        'DROP TABLE IF EXISTS customer_activity_log',
        'DROP TABLE IF EXISTS license_seats',
        'DROP TABLE IF EXISTS admin_audit_log',
        'DROP TABLE IF EXISTS used_nonces',
        'DROP TABLE IF EXISTS activation_logs',
        'DROP TABLE IF EXISTS discount_codes',
        'DROP TABLE IF EXISTS billing_history',
        'DROP TABLE IF EXISTS invoice_items',
        'DROP TABLE IF EXISTS invoices',
        'DROP TABLE IF EXISTS subscriptions',
        'DROP TABLE IF EXISTS licenses',
        'DROP TABLE IF EXISTS payment_methods',
        'DROP TABLE IF EXISTS customers',
        'DROP TABLE IF EXISTS billing_plans',
        'DROP TABLE IF EXISTS admin_users',
        'DROP TABLE IF EXISTS settings',
    ]


# ============================================================================
# Default Data
# ============================================================================

def get_default_data() -> dict:
    """Get default data to insert after table creation."""
    now = datetime.now(UTC).isoformat()

    return {
        'billing_plans': [
            {
                'id': 'plan_trial',
                'name': 'trial',
                'display_name': 'Trial',
                'description': '14-day free trial with full features',
                'license_type': 0,
                'price_monthly': 0,
                'price_yearly': 0,
                'price_lifetime': 0,
                'max_seats': 1,
                'features': '["Basic browsing", "14 day limit"]',
                'is_active': 1,
                'sort_order': 0,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'plan_starter',
                'name': 'starter',
                'display_name': 'Starter',
                'description': 'Monthly subscription for individuals and small teams',
                'license_type': 1,
                'price_monthly': 1999.00,
                'price_yearly': 0,
                'price_lifetime': 0,
                'max_seats': 3,
                'features': '["Full browser automation", "3 device seats", "Email support", "Monthly billing"]',
                'is_active': 1,
                'sort_order': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'plan_business',
                'name': 'business',
                'display_name': 'Business',
                'description': 'One-time payment for growing teams (1 year license)',
                'license_type': 2,
                'price_monthly': 3999.00,
                'price_yearly': 19999.00,
                'price_lifetime': 0,
                'max_seats': 10,
                'features': '["All Starter features", "10 device seats", "1 year license", "Priority support", "Optional $3,999/mo maintenance"]',
                'is_active': 1,
                'sort_order': 2,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'plan_enterprise',
                'name': 'enterprise',
                'display_name': 'Enterprise',
                'description': 'One-time payment for large organizations (1 year license)',
                'license_type': 3,
                'price_monthly': 9999.00,
                'price_yearly': 49999.00,
                'price_lifetime': 0,
                'max_seats': 50,
                'features': '["All Business features", "50 device seats", "1 year license", "Dedicated support", "SLA", "Optional $9,999/mo maintenance"]',
                'is_active': 1,
                'sort_order': 3,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'plan_developer',
                'name': 'developer',
                'display_name': 'Developer',
                'description': 'For developers building integrations',
                'license_type': 4,
                'price_monthly': 49.99,
                'price_yearly': 499.99,
                'price_lifetime': 799.99,
                'max_seats': 5,
                'features': '["Full API access", "SDK access", "Developer documentation", "Technical support"]',
                'is_active': 1,
                'sort_order': 4,
                'created_at': now,
                'updated_at': now
            }
        ],
        'email_templates': [
            {
                'id': 'tpl_welcome',
                'name': 'welcome',
                'subject': 'Welcome to Owl Browser!',
                'body_html': '<h1>Welcome, {{name}}!</h1><p>Thank you for choosing Owl Browser.</p>',
                'body_text': 'Welcome, {{name}}! Thank you for choosing Owl Browser.',
                'variables': 'name,email',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_license_issued',
                'name': 'license_issued',
                'subject': 'Your Owl Browser License',
                'body_html': '<h1>Your License is Ready</h1><p>Dear {{name}},</p><p>Your {{plan}} license has been issued.</p>',
                'body_text': 'Your License is Ready. Dear {{name}}, Your {{plan}} license has been issued.',
                'variables': 'name,email,plan,license_id',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_payment_received',
                'name': 'payment_received',
                'subject': 'Payment Received - Owl Browser',
                'body_html': '<h1>Payment Received</h1><p>Thank you for your payment of {{amount}}.</p>',
                'body_text': 'Payment Received. Thank you for your payment of {{amount}}.',
                'variables': 'name,amount,invoice_number',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_subscription_expiring',
                'name': 'subscription_expiring',
                'subject': 'Your Owl Browser subscription is expiring soon',
                'body_html': '<h1>Subscription Expiring</h1><p>Dear {{customer_name}},</p><p>Your subscription expires on {{expiry_date}}. You have {{days_remaining}} days remaining.</p><p>Please renew to continue using Owl Browser.</p>',
                'body_text': 'Dear {{customer_name}}, Your subscription expires on {{expiry_date}}. You have {{days_remaining}} days remaining. Please renew to continue using Owl Browser.',
                'variables': 'customer_name,expiry_date,days_remaining,license_name',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_invoice_created',
                'name': 'invoice_created',
                'subject': 'New Invoice {{invoice_number}} - Owl Browser',
                'body_html': '''<h1>Invoice Created</h1>
<p>Dear {{customer_name}},</p>
<p>A new invoice has been created for your account.</p>
<table style="border-collapse: collapse; width: 100%; max-width: 400px;">
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Invoice Number:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{invoice_number}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Amount:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{invoice_total}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Due Date:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{due_date}}</td></tr>
</table>
<p>Please ensure payment is made by the due date to avoid any service interruption.</p>
<p>Thank you for your business!</p>''',
                'body_text': 'Dear {{customer_name}}, A new invoice ({{invoice_number}}) has been created for {{invoice_total}}. Due date: {{due_date}}. Please ensure payment is made by the due date.',
                'variables': 'customer_name,invoice_number,invoice_total,due_date',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_invoice_reminder',
                'name': 'invoice_reminder',
                'subject': 'Payment Reminder - Invoice {{invoice_number}}',
                'body_html': '''<h1>Payment Reminder</h1>
<p>Dear {{customer_name}},</p>
<p>This is a friendly reminder that your invoice is due soon.</p>
<table style="border-collapse: collapse; width: 100%; max-width: 400px;">
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Invoice Number:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{invoice_number}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Amount Due:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{amount_due}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Due Date:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{due_date}}</td></tr>
</table>
<p>Please make your payment to avoid any service interruption.</p>''',
                'body_text': 'Dear {{customer_name}}, This is a reminder that invoice {{invoice_number}} for {{amount_due}} is due on {{due_date}}. Please make your payment soon.',
                'variables': 'customer_name,invoice_number,amount_due,due_date',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_invoice_overdue',
                'name': 'invoice_overdue',
                'subject': 'OVERDUE: Invoice {{invoice_number}} - Action Required',
                'body_html': '''<h1 style="color: #dc3545;">Invoice Overdue</h1>
<p>Dear {{customer_name}},</p>
<p><strong>Your invoice is now {{days_overdue}} days overdue.</strong></p>
<table style="border-collapse: collapse; width: 100%; max-width: 400px;">
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Invoice Number:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{invoice_number}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Amount Due:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{amount_due}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Original Due Date:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{due_date}}</td></tr>
</table>
<p style="color: #dc3545;"><strong>Please make payment immediately to avoid suspension of your license.</strong></p>
<p>If you have already made payment, please disregard this notice.</p>''',
                'body_text': 'OVERDUE: Dear {{customer_name}}, Invoice {{invoice_number}} for {{amount_due}} is {{days_overdue}} days overdue. Original due date: {{due_date}}. Please make payment immediately to avoid suspension of your license.',
                'variables': 'customer_name,invoice_number,amount_due,due_date,days_overdue',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_license_expired',
                'name': 'license_expired',
                'subject': 'Your Owl Browser License Has Expired',
                'body_html': '''<h1>License Expired</h1>
<p>Dear {{customer_name}},</p>
<p>Your Owl Browser license has expired.</p>
<table style="border-collapse: collapse; width: 100%; max-width: 400px;">
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>License:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{license_name}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Expired On:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{expiry_date}}</td></tr>
</table>
<p>To continue using Owl Browser, please renew your license or contact support.</p>''',
                'body_text': 'Dear {{customer_name}}, Your Owl Browser license ({{license_name}}) has expired on {{expiry_date}}. Please renew to continue using the service.',
                'variables': 'customer_name,license_name,license_id,expiry_date',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_license_suspended',
                'name': 'license_suspended',
                'subject': 'Your Owl Browser License Has Been Suspended',
                'body_html': '''<h1 style="color: #dc3545;">License Suspended</h1>
<p>Dear {{customer_name}},</p>
<p>Your Owl Browser license has been suspended due to non-payment.</p>
<table style="border-collapse: collapse; width: 100%; max-width: 400px;">
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>License:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{license_name}}</td></tr>
<tr><td style="padding: 8px; border-bottom: 1px solid #ddd;"><strong>Outstanding Amount:</strong></td><td style="padding: 8px; border-bottom: 1px solid #ddd;">{{amount_due}}</td></tr>
</table>
<p>To reactivate your license, please pay the outstanding balance immediately.</p>
<p>If you believe this is an error, please contact support.</p>''',
                'body_text': 'Dear {{customer_name}}, Your Owl Browser license ({{license_name}}) has been suspended due to non-payment. Outstanding amount: {{amount_due}}. Please pay immediately to reactivate.',
                'variables': 'customer_name,license_name,amount_due',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_password_reset',
                'name': 'password_reset',
                'subject': 'Reset Your Password - Owl Browser',
                'body_html': '''<h1>Password Reset Request</h1>
<p>Dear {{customer_name}},</p>
<p>We received a request to reset the password for your Owl Browser account.</p>
<p>Click the link below to reset your password:</p>
<p style="text-align: center; margin: 30px 0;">
    <a href="{{reset_link}}" style="background: #4e9179; color: white; padding: 12px 24px; text-decoration: none; border-radius: 6px; display: inline-block;">Reset Password</a>
</p>
<p>If you did not request a password reset, you can safely ignore this email. Your password will remain unchanged.</p>
<p>This link will expire in 24 hours.</p>
<p><small class="text-muted">If the button above doesn't work, copy and paste this link into your browser: {{reset_link}}</small></p>''',
                'body_text': 'Dear {{customer_name}}, We received a request to reset your password. Visit this link to reset your password: {{reset_link}} - This link expires in 24 hours. If you did not request this, ignore this email.',
                'variables': 'customer_name,email,reset_link',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            },
            {
                'id': 'tpl_email_verification',
                'name': 'email_verification',
                'subject': 'Verify Your Email - Owl Browser',
                'body_html': '''<h1>Email Verification</h1>
<p>Dear {{customer_name}},</p>
<p>Thank you for registering with Owl Browser. Please verify your email address by clicking the link below:</p>
<p style="text-align: center; margin: 30px 0;">
    <a href="{{verification_link}}" style="background: #4e9179; color: white; padding: 12px 24px; text-decoration: none; border-radius: 6px; display: inline-block;">Verify Email</a>
</p>
<p>If you did not create an account, you can safely ignore this email.</p>
<p><small class="text-muted">If the button above doesn't work, copy and paste this link into your browser: {{verification_link}}</small></p>''',
                'body_text': 'Dear {{customer_name}}, Thank you for registering. Please verify your email by visiting: {{verification_link}}',
                'variables': 'customer_name,email,verification_link',
                'is_active': 1,
                'created_at': now,
                'updated_at': now
            }
        ],
        'settings': [
            {'key': 'company_name', 'value': 'Olib AI', 'type': 'string', 'description': 'Company name', 'updated_at': now},
            {'key': 'company_email', 'value': 'support@owlbrowser.net', 'type': 'string', 'description': 'Support email', 'updated_at': now},
            {'key': 'company_website', 'value': 'https://owlbrowser.net', 'type': 'string', 'description': 'Website URL', 'updated_at': now},
            {'key': 'trial_days', 'value': '14', 'type': 'integer', 'description': 'Trial period in days', 'updated_at': now},
            {'key': 'grace_period_days', 'value': '7', 'type': 'integer', 'description': 'Subscription grace period', 'updated_at': now},
            {'key': 'invoice_prefix', 'value': 'OWL-', 'type': 'string', 'description': 'Invoice number prefix', 'updated_at': now},
            {'key': 'tax_rate', 'value': '0', 'type': 'float', 'description': 'Default tax rate (%)', 'updated_at': now},
            {'key': 'currency', 'value': 'USD', 'type': 'string', 'description': 'Default currency', 'updated_at': now},
            # SMTP Email Settings
            {'key': 'smtp_host', 'value': '', 'type': 'string', 'description': 'SMTP server hostname', 'updated_at': now},
            {'key': 'smtp_port', 'value': '587', 'type': 'integer', 'description': 'SMTP server port', 'updated_at': now},
            {'key': 'smtp_username', 'value': '', 'type': 'string', 'description': 'SMTP username', 'updated_at': now},
            {'key': 'smtp_password', 'value': '', 'type': 'string', 'description': 'SMTP password', 'updated_at': now},
            {'key': 'smtp_tls', 'value': 'true', 'type': 'boolean', 'description': 'Use TLS for SMTP', 'updated_at': now},
            {'key': 'smtp_from_email', 'value': '', 'type': 'string', 'description': 'From email address', 'updated_at': now},
            {'key': 'smtp_from_name', 'value': 'Owl Browser Licensing', 'type': 'string', 'description': 'From name', 'updated_at': now},
        ]
    }


# ============================================================================
# Database Initializer
# ============================================================================

class DatabaseInitializer:
    """Database initialization handler."""

    # Table names in creation order
    TABLE_NAMES = [
        'admin_users', 'billing_plans', 'customers', 'licenses',
        'customer_users', 'customer_sessions', 'payment_methods',
        'subscriptions', 'invoices', 'invoice_items',
        'billing_history', 'discount_codes', 'activation_logs', 'used_nonces',
        'admin_audit_log', 'license_seats', 'customer_activity_log',
        'email_templates', 'email_log', 'settings', 'invoice_reminders'
    ]

    def __init__(self, db_type: str = None):
        self.db_type = db_type or DATABASE_TYPE
        self.connection = None

    def connect(self):
        """Establish database connection."""
        if self.db_type == 'sqlite':
            import sqlite3
            db_path = DATABASE_URL
            # Ensure the parent directory exists
            Path(db_path).parent.mkdir(parents=True, exist_ok=True)
            self.connection = sqlite3.connect(db_path)
            print(f"Connected to SQLite database: {db_path}")

        elif self.db_type == 'postgresql':
            import psycopg2
            self.connection = psycopg2.connect(
                host=DB_HOST,
                port=DB_PORT,
                dbname=DB_NAME,
                user=DB_USER,
                password=DB_PASSWORD
            )
            print(f"Connected to PostgreSQL database: {DB_NAME}@{DB_HOST}:{DB_PORT}")

        elif self.db_type == 'mysql':
            import mysql.connector
            self.connection = mysql.connector.connect(
                host=DB_HOST,
                port=DB_PORT,
                database=DB_NAME,
                user=DB_USER,
                password=DB_PASSWORD
            )
            print(f"Connected to MySQL database: {DB_NAME}@{DB_HOST}:{DB_PORT}")

        else:
            raise ValueError(f"Unsupported database type: {self.db_type}")

    def close(self):
        """Close database connection."""
        if self.connection:
            self.connection.close()
            self.connection = None

    def execute(self, sql: str, params: tuple = None):
        """Execute a SQL statement."""
        cursor = self.connection.cursor()
        if params:
            cursor.execute(sql, params)
        else:
            cursor.execute(sql)
        cursor.close()

    def commit(self):
        """Commit transaction."""
        self.connection.commit()

    def get_schema(self) -> list:
        """Get schema statements for current database type."""
        if self.db_type == 'postgresql':
            return get_postgresql_schema()
        elif self.db_type == 'mysql':
            return get_mysql_schema()
        else:
            return get_sqlite_schema()

    def table_exists(self, table_name: str) -> bool:
        """Check if a table exists."""
        cursor = self.connection.cursor()

        try:
            if self.db_type == 'sqlite':
                cursor.execute(
                    "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
                    (table_name,)
                )
            elif self.db_type == 'postgresql':
                cursor.execute(
                    "SELECT tablename FROM pg_tables WHERE tablename = %s",
                    (table_name,)
                )
            elif self.db_type == 'mysql':
                cursor.execute(
                    "SHOW TABLES LIKE %s",
                    (table_name,)
                )

            result = cursor.fetchone() is not None
        except Exception:
            result = False
        finally:
            cursor.close()

        return result

    def get_table_count(self, table_name: str) -> int:
        """Get row count for a table."""
        cursor = self.connection.cursor()
        try:
            cursor.execute(f"SELECT COUNT(*) FROM {table_name}")
            count = cursor.fetchone()[0]
        except Exception:
            count = 0
        finally:
            cursor.close()
        return count

    def drop_tables(self):
        """Drop all tables."""
        print("\nDropping existing tables...")
        for sql in get_drop_statements():
            try:
                self.execute(sql)
                table_name = sql.split()[-1]
                print(f"  Dropped: {table_name}")
            except Exception as e:
                print(f"  Warning: {e}")
        self.commit()

    def create_tables(self):
        """Create all tables."""
        print("\nCreating tables...")
        schema = self.get_schema()

        for i, sql in enumerate(schema):
            try:
                self.execute(sql)
                print(f"  Created: {self.TABLE_NAMES[i]}")
            except Exception as e:
                print(f"  Error creating {self.TABLE_NAMES[i]}: {e}")
                raise
        self.commit()

    def create_indexes(self):
        """Create all indexes."""
        print("\nCreating indexes...")
        for sql in get_index_statements(self.db_type):
            try:
                self.execute(sql)
                idx_name = sql.split()[5]  # Extract index name
                print(f"  Created: {idx_name}")
            except Exception:
                # Index may already exist
                pass
        self.commit()

    def insert_default_data(self):
        """Insert default data into tables."""
        print("\nInserting default data...")
        defaults = get_default_data()
        ph = '?' if self.db_type == 'sqlite' else '%s'

        # Columns that need boolean conversion for PostgreSQL
        boolean_columns = {'is_active'}

        for table, rows in defaults.items():
            if not rows:
                continue

            # Check if data already exists
            if self.get_table_count(table) > 0:
                print(f"  Skipped: {table} (already has data)")
                continue

            columns = list(rows[0].keys())
            placeholders = ', '.join([ph] * len(columns))
            col_names = ', '.join(columns)

            for row in rows:
                # Convert integer booleans to Python booleans for PostgreSQL
                if self.db_type == 'postgresql':
                    converted_values = []
                    for col, val in row.items():
                        if col in boolean_columns and isinstance(val, int):
                            converted_values.append(bool(val))
                        else:
                            converted_values.append(val)
                    values = tuple(converted_values)
                else:
                    values = tuple(row.values())

                try:
                    self.execute(
                        f"INSERT INTO {table} ({col_names}) VALUES ({placeholders})",
                        values
                    )
                except Exception as e:
                    print(f"  Error inserting into {table}: {e}")

            print(f"  Inserted: {table} ({len(rows)} rows)")

        self.commit()

    def check_database(self) -> dict:
        """Check database status and return info."""
        info = {
            'type': self.db_type,
            'tables': {},
            'exists': False
        }

        for table in self.TABLE_NAMES:
            if self.table_exists(table):
                info['exists'] = True
                info['tables'][table] = {
                    'exists': True,
                    'count': self.get_table_count(table)
                }
            else:
                info['tables'][table] = {'exists': False, 'count': 0}

        return info

    def backup_sqlite(self) -> str:
        """Backup SQLite database file."""
        if self.db_type != 'sqlite':
            print("Backup only supported for SQLite in this script.")
            return None

        db_path = DATABASE_URL
        if not os.path.exists(db_path):
            print("No existing database to backup.")
            return None

        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        backup_path = f"{db_path}.backup_{timestamp}"
        shutil.copy2(db_path, backup_path)
        print(f"Database backed up to: {backup_path}")
        return backup_path


def print_database_info(info: dict):
    """Print database information."""
    print(f"\nDatabase Type: {info['type'].upper()}")
    print(f"Database Exists: {'Yes' if info['exists'] else 'No'}")

    if info['exists'] or info['tables']:
        print("\nTable Status:")
        print("-" * 60)
        total_rows = 0
        for table, data in info['tables'].items():
            status = "OK" if data['exists'] else "MISSING"
            count = data['count']
            total_rows += count
            print(f"  {table:<30} {status:<10} {count:>8} rows")
        print("-" * 60)
        print(f"  {'Total':<30} {'':<10} {total_rows:>8} rows")


def main():
    parser = argparse.ArgumentParser(
        description='Initialize Owl Browser License Database',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  python3 init_database.py              # Create tables (safe, won't overwrite)
  python3 init_database.py --check      # Check database status
  python3 init_database.py --force      # Drop and recreate all tables
  python3 init_database.py --backup     # Backup SQLite before initializing
  python3 init_database.py --backup --force  # Backup, then drop and recreate
  python3 init_database.py --no-defaults     # Don't insert default data

Tables Created:
  - admin_users, customers, billing_plans, payment_methods
  - licenses, subscriptions, invoices, invoice_items
  - billing_history, discount_codes, activation_logs, used_nonces
  - admin_audit_log, license_seats, customer_activity_log
  - email_templates, email_log, settings
        '''
    )

    parser.add_argument('--force', '-f', action='store_true',
                       help='Force drop and recreate all tables (DESTRUCTIVE!)')
    parser.add_argument('--yes', '-y', action='store_true',
                       help='Skip confirmation prompt (use with --force)')
    parser.add_argument('--backup', '-b', action='store_true',
                       help='Backup SQLite database before any changes')
    parser.add_argument('--check', '-c', action='store_true',
                       help='Only check database status, do not modify')
    parser.add_argument('--type', '-t', choices=['sqlite', 'postgresql', 'mysql'],
                       help=f'Database type (default: {DATABASE_TYPE})')
    parser.add_argument('--no-defaults', action='store_true',
                       help='Do not insert default data (plans, templates, settings)')

    args = parser.parse_args()

    db_type = args.type or DATABASE_TYPE
    print(f"Owl Browser License Database Initializer")
    print(f"=" * 60)
    print(f"Database type: {db_type}")

    if db_type == 'sqlite':
        print(f"Database path: {DATABASE_URL}")
    else:
        print(f"Database host: {DB_HOST}:{DB_PORT}")
        print(f"Database name: {DB_NAME}")

    initializer = DatabaseInitializer(db_type)

    try:
        initializer.connect()

        # Check mode - just show info
        if args.check:
            info = initializer.check_database()
            print_database_info(info)
            initializer.close()
            return

        # Backup if requested (SQLite only)
        if args.backup:
            initializer.backup_sqlite()

        # Force mode - drop all tables first
        if args.force:
            print("\n" + "!" * 60)
            print("WARNING: This will DELETE ALL DATA!")
            print("!" * 60)

            if not args.yes:
                confirm = input("\nType 'YES' to confirm: ")
                if confirm != 'YES':
                    print("Aborted.")
                    initializer.close()
                    return
            else:
                print("\nConfirmation skipped (--yes flag)")

            initializer.drop_tables()

        # Create tables and indexes
        initializer.create_tables()
        initializer.create_indexes()

        # Insert default data unless --no-defaults
        if not args.no_defaults:
            initializer.insert_default_data()

        # Show final status
        info = initializer.check_database()
        print_database_info(info)

        print("\nDatabase initialization complete!")

    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    finally:
        initializer.close()


if __name__ == '__main__':
    main()
