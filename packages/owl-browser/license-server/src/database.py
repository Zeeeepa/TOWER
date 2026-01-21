"""
Owl Browser License Database Abstraction Layer

Provides a unified interface for different database backends:
- SQLite (default, for development)
- PostgreSQL (production)
- MySQL (alternative production)

The abstraction allows easy switching between databases without code changes.

Tables:
  Core: admin_users, billing_plans, customers, payment_methods
  Licenses: licenses, subscriptions, license_seats
  Billing: invoices, invoice_items, billing_history, discount_codes
  Audit: admin_audit_log, activation_logs, used_nonces, customer_activity_log
  Email: email_templates, email_log
  Config: settings
"""

import sqlite3
import uuid
import json
from datetime import datetime, timedelta, UTC
from abc import ABC, abstractmethod
from typing import Optional, List, Dict, Any, Tuple
from contextlib import contextmanager
import threading

from config import (
    DATABASE_TYPE, DATABASE_URL, DB_HOST, DB_PORT,
    DB_NAME, DB_USER, DB_PASSWORD, LICENSE_TYPES
)


# ============================================================================
# Database Backend Interfaces
# ============================================================================

class DatabaseBackend(ABC):
    """Abstract base class for database backends."""

    @abstractmethod
    def connect(self):
        """Establish database connection."""
        pass

    @abstractmethod
    def close(self):
        """Close database connection."""
        pass

    @abstractmethod
    def execute(self, query: str, params: tuple = ()) -> Any:
        """Execute a query and return cursor."""
        pass

    @abstractmethod
    def executemany(self, query: str, params_list: List[tuple]) -> Any:
        """Execute a query with multiple parameter sets."""
        pass

    @abstractmethod
    def fetchone(self, query: str, params: tuple = ()) -> Optional[tuple]:
        """Fetch one row."""
        pass

    @abstractmethod
    def fetchall(self, query: str, params: tuple = ()) -> List[tuple]:
        """Fetch all rows."""
        pass

    @abstractmethod
    def commit(self):
        """Commit transaction."""
        pass

    @abstractmethod
    def rollback(self):
        """Rollback transaction."""
        pass

    @abstractmethod
    def get_placeholder(self) -> str:
        """Get parameter placeholder (? for SQLite, %s for PostgreSQL/MySQL)."""
        pass


class SQLiteBackend(DatabaseBackend):
    """SQLite database backend."""

    def __init__(self, db_path: str):
        self.db_path = db_path
        self._local = threading.local()

    @property
    def connection(self):
        if not hasattr(self._local, 'connection') or self._local.connection is None:
            self._local.connection = sqlite3.connect(
                self.db_path,
                check_same_thread=False,
                detect_types=sqlite3.PARSE_DECLTYPES | sqlite3.PARSE_COLNAMES
            )
            self._local.connection.row_factory = sqlite3.Row
        return self._local.connection

    def connect(self):
        return self.connection

    def close(self):
        if hasattr(self._local, 'connection') and self._local.connection:
            self._local.connection.close()
            self._local.connection = None

    def execute(self, query: str, params: tuple = ()) -> Any:
        return self.connection.execute(query, params)

    def executemany(self, query: str, params_list: List[tuple]) -> Any:
        return self.connection.executemany(query, params_list)

    def fetchone(self, query: str, params: tuple = ()) -> Optional[sqlite3.Row]:
        cursor = self.connection.execute(query, params)
        return cursor.fetchone()

    def fetchall(self, query: str, params: tuple = ()) -> List[sqlite3.Row]:
        cursor = self.connection.execute(query, params)
        return cursor.fetchall()

    def commit(self):
        self.connection.commit()

    def rollback(self):
        self.connection.rollback()

    def get_placeholder(self) -> str:
        return '?'

    def lastrowid(self) -> int:
        """Get last inserted row ID."""
        cursor = self.connection.execute("SELECT last_insert_rowid()")
        return cursor.fetchone()[0]


class PostgreSQLBackend(DatabaseBackend):
    """PostgreSQL database backend."""

    def __init__(self, host: str, port: int, dbname: str, user: str, password: str):
        self.config = {
            'host': host,
            'port': port,
            'dbname': dbname,
            'user': user,
            'password': password
        }
        self._connection = None

    def connect(self):
        if self._connection is None:
            import psycopg2
            import psycopg2.extras
            self._connection = psycopg2.connect(**self.config)
            self._connection.cursor_factory = psycopg2.extras.RealDictCursor
        return self._connection

    def close(self):
        if self._connection:
            self._connection.close()
            self._connection = None

    def execute(self, query: str, params: tuple = ()) -> Any:
        try:
            cursor = self.connect().cursor()
            cursor.execute(query, params)
            return cursor
        except Exception:
            self.rollback()
            raise

    def executemany(self, query: str, params_list: List[tuple]) -> Any:
        try:
            cursor = self.connect().cursor()
            cursor.executemany(query, params_list)
            return cursor
        except Exception:
            self.rollback()
            raise

    def fetchone(self, query: str, params: tuple = ()) -> Optional[Dict]:
        try:
            cursor = self.connect().cursor()
            cursor.execute(query, params)
            return cursor.fetchone()
        except Exception:
            self.rollback()
            raise

    def fetchall(self, query: str, params: tuple = ()) -> List[Dict]:
        try:
            cursor = self.connect().cursor()
            cursor.execute(query, params)
            return cursor.fetchall()
        except Exception:
            self.rollback()
            raise

    def commit(self):
        if self._connection:
            self._connection.commit()

    def rollback(self):
        if self._connection:
            self._connection.rollback()

    def get_placeholder(self) -> str:
        return '%s'


class MySQLBackend(DatabaseBackend):
    """MySQL database backend."""

    def __init__(self, host: str, port: int, dbname: str, user: str, password: str):
        self.config = {
            'host': host,
            'port': port,
            'database': dbname,
            'user': user,
            'password': password
        }
        self._connection = None

    def connect(self):
        if self._connection is None:
            import mysql.connector
            self._connection = mysql.connector.connect(**self.config, dictionary=True)
        return self._connection

    def close(self):
        if self._connection:
            self._connection.close()
            self._connection = None

    def execute(self, query: str, params: tuple = ()) -> Any:
        cursor = self.connect().cursor()
        cursor.execute(query, params)
        return cursor

    def executemany(self, query: str, params_list: List[tuple]) -> Any:
        cursor = self.connect().cursor()
        cursor.executemany(query, params_list)
        return cursor

    def fetchone(self, query: str, params: tuple = ()) -> Optional[Dict]:
        cursor = self.connect().cursor()
        cursor.execute(query, params)
        return cursor.fetchone()

    def fetchall(self, query: str, params: tuple = ()) -> List[Dict]:
        cursor = self.connect().cursor()
        cursor.execute(query, params)
        return cursor.fetchall()

    def commit(self):
        if self._connection:
            self._connection.commit()

    def rollback(self):
        if self._connection:
            self._connection.rollback()

    def get_placeholder(self) -> str:
        return '%s'


# ============================================================================
# License Database - Main Interface
# ============================================================================

class LicenseDatabase:
    """
    High-level license database interface.

    Provides CRUD operations for all tables independent of the underlying
    database backend.
    """

    def __init__(self, backend: Optional[DatabaseBackend] = None):
        if backend is None:
            backend = self._create_default_backend()
        self.backend = backend
        self._ensure_tables()

    def _create_default_backend(self) -> DatabaseBackend:
        """Create default backend based on configuration."""
        if DATABASE_TYPE == 'postgresql':
            return PostgreSQLBackend(DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD)
        elif DATABASE_TYPE == 'mysql':
            return MySQLBackend(DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD)
        else:
            return SQLiteBackend(DATABASE_URL)

    def _ensure_tables(self):
        """Verify tables exist (they should be created by init_database.py)."""
        # Just verify connection works
        try:
            self.backend.connect()
        except Exception as e:
            print(f"Warning: Could not connect to database: {e}")

    def _bool_val(self, value: bool) -> Any:
        """Return database-appropriate boolean value.

        SQLite uses INTEGER (1/0), PostgreSQL uses BOOLEAN (True/False).
        """
        if DATABASE_TYPE == 'postgresql':
            return value
        else:
            return 1 if value else 0

    @contextmanager
    def transaction(self):
        """Context manager for database transactions."""
        try:
            yield
            self.backend.commit()
        except Exception as e:
            self.backend.rollback()
            raise e

    def _generate_id(self) -> str:
        """Generate a new UUID."""
        return str(uuid.uuid4())

    def _now(self) -> str:
        """Get current UTC timestamp as ISO string."""
        return datetime.now(UTC).isoformat()

    # ========================================================================
    # Admin Users CRUD
    # ========================================================================

    def create_admin_user(self, data: Dict[str, Any]) -> str:
        """Create a new admin user."""
        user_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        self.backend.execute(f'''
            INSERT INTO admin_users (
                id, username, email, password_hash, role, is_active,
                created_at, updated_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            user_id,
            data['username'],
            data['email'],
            data['password_hash'],
            data.get('role', 'admin'),
            self._bool_val(data.get('is_active', True)),
            now,
            now
        ))
        self.backend.commit()
        return user_id

    def get_admin_user(self, user_id: str) -> Optional[Dict[str, Any]]:
        """Get admin user by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM admin_users WHERE id = {ph}',
            (user_id,)
        )
        return dict(row) if row else None

    def get_admin_user_by_username(self, username: str) -> Optional[Dict[str, Any]]:
        """Get admin user by username."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM admin_users WHERE username = {ph}',
            (username,)
        )
        return dict(row) if row else None

    def get_admin_user_by_email(self, email: str) -> Optional[Dict[str, Any]]:
        """Get admin user by email."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM admin_users WHERE email = {ph}',
            (email,)
        )
        return dict(row) if row else None

    def get_all_admin_users(self) -> List[Dict[str, Any]]:
        """Get all admin users."""
        rows = self.backend.fetchall(
            'SELECT * FROM admin_users ORDER BY created_at DESC'
        )
        return [dict(row) for row in rows]

    def update_admin_user(self, user_id: str, updates: Dict[str, Any]) -> bool:
        """Update admin user."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [user_id]

        self.backend.execute(
            f'UPDATE admin_users SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def update_admin_last_login(self, user_id: str) -> bool:
        """Update admin user's last login timestamp."""
        ph = self.backend.get_placeholder()
        self.backend.execute(
            f'UPDATE admin_users SET last_login_at = {ph} WHERE id = {ph}',
            (self._now(), user_id)
        )
        self.backend.commit()
        return True

    # ========================================================================
    # Billing Plans CRUD
    # ========================================================================

    def create_billing_plan(self, data: Dict[str, Any]) -> str:
        """Create a new billing plan."""
        plan_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        features = data.get('features', [])
        if isinstance(features, list):
            features = json.dumps(features)

        self.backend.execute(f'''
            INSERT INTO billing_plans (
                id, name, display_name, description, license_type,
                price_monthly, price_yearly, price_lifetime, currency,
                max_seats, features, is_active, sort_order, created_at, updated_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            plan_id,
            data['name'],
            data['display_name'],
            data.get('description', ''),
            data['license_type'],
            data.get('price_monthly', 0),
            data.get('price_yearly', 0),
            data.get('price_lifetime', 0),
            data.get('currency', 'USD'),
            data.get('max_seats', 1),
            features,
            self._bool_val(data.get('is_active', True)),
            data.get('sort_order', 0),
            now,
            now
        ))
        self.backend.commit()
        return plan_id

    def get_billing_plan(self, plan_id: str) -> Optional[Dict[str, Any]]:
        """Get billing plan by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM billing_plans WHERE id = {ph}',
            (plan_id,)
        )
        if row:
            result = dict(row)
            if result.get('features') and isinstance(result['features'], str):
                try:
                    result['features'] = json.loads(result['features'])
                except:
                    pass
            return result
        return None

    def get_billing_plan_by_name(self, name: str) -> Optional[Dict[str, Any]]:
        """Get billing plan by name."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM billing_plans WHERE name = {ph}',
            (name,)
        )
        if row:
            result = dict(row)
            if result.get('features') and isinstance(result['features'], str):
                try:
                    result['features'] = json.loads(result['features'])
                except:
                    pass
            return result
        return None

    def get_all_billing_plans(self, active_only: bool = False) -> List[Dict[str, Any]]:
        """Get all billing plans."""
        ph = self.backend.get_placeholder()
        if active_only:
            rows = self.backend.fetchall(
                f'SELECT * FROM billing_plans WHERE is_active = {ph} ORDER BY sort_order',
                (self._bool_val(True),)
            )
        else:
            rows = self.backend.fetchall(
                'SELECT * FROM billing_plans ORDER BY sort_order'
            )
        results = []
        for row in rows:
            result = dict(row)
            if result.get('features') and isinstance(result['features'], str):
                try:
                    result['features'] = json.loads(result['features'])
                except:
                    pass
            results.append(result)
        return results

    def update_billing_plan(self, plan_id: str, updates: Dict[str, Any]) -> bool:
        """Update billing plan."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        if 'features' in updates and isinstance(updates['features'], list):
            updates['features'] = json.dumps(updates['features'])

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [plan_id]

        self.backend.execute(
            f'UPDATE billing_plans SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    # ========================================================================
    # Customers CRUD
    # ========================================================================

    def create_customer(self, data: Dict[str, Any]) -> str:
        """Create a new customer."""
        customer_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        self.backend.execute(f'''
            INSERT INTO customers (
                id, email, name, company, phone,
                address_line1, address_line2, city, state, postal_code, country,
                tax_id, stripe_customer_id, paypal_customer_id, notes, status,
                created_at, updated_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            customer_id,
            data['email'],
            data['name'],
            data.get('company', ''),
            data.get('phone', ''),
            data.get('address_line1', ''),
            data.get('address_line2', ''),
            data.get('city', ''),
            data.get('state', ''),
            data.get('postal_code', ''),
            data.get('country', 'US'),
            data.get('tax_id', ''),
            data.get('stripe_customer_id', ''),
            data.get('paypal_customer_id', ''),
            data.get('notes', ''),
            data.get('status', 'active'),
            now,
            now
        ))
        self.backend.commit()
        return customer_id

    def get_customer(self, customer_id: str) -> Optional[Dict[str, Any]]:
        """Get customer by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customers WHERE id = {ph}',
            (customer_id,)
        )
        return dict(row) if row else None

    def get_customer_by_email(self, email: str) -> Optional[Dict[str, Any]]:
        """Get customer by email."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customers WHERE email = {ph}',
            (email,)
        )
        return dict(row) if row else None

    def get_customer_by_stripe_id(self, stripe_id: str) -> Optional[Dict[str, Any]]:
        """Get customer by Stripe customer ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customers WHERE stripe_customer_id = {ph}',
            (stripe_id,)
        )
        return dict(row) if row else None

    def get_all_customers(self,
                          status: Optional[str] = None,
                          limit: int = 100,
                          offset: int = 0,
                          search: Optional[str] = None) -> List[Dict[str, Any]]:
        """Get all customers with optional filtering."""
        ph = self.backend.get_placeholder()
        conditions = []
        params = []

        if status:
            conditions.append(f'c.status = {ph}')
            params.append(status)
        if search:
            conditions.append(f'(c.email LIKE {ph} OR c.name LIKE {ph} OR c.company LIKE {ph})')
            search_term = f'%{search}%'
            params.extend([search_term, search_term, search_term])

        where_clause = ' AND '.join(conditions) if conditions else '1=1'

        rows = self.backend.fetchall(
            f'''SELECT c.*, COUNT(l.id) as license_count
                FROM customers c
                LEFT JOIN licenses l ON l.customer_id = c.id
                WHERE {where_clause}
                GROUP BY c.id
                ORDER BY c.created_at DESC
                LIMIT {ph} OFFSET {ph}''',
            tuple(params) + (limit, offset)
        )
        return [dict(row) for row in rows]

    def get_customer_count(self, status: Optional[str] = None) -> int:
        """Get total customer count."""
        ph = self.backend.get_placeholder()
        if status:
            row = self.backend.fetchone(
                f'SELECT COUNT(*) as count FROM customers WHERE status = {ph}',
                (status,)
            )
        else:
            row = self.backend.fetchone('SELECT COUNT(*) as count FROM customers')
        return row['count'] if row else 0

    def update_customer(self, customer_id: str, updates: Dict[str, Any]) -> bool:
        """Update customer."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [customer_id]

        self.backend.execute(
            f'UPDATE customers SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def delete_customer(self, customer_id: str) -> bool:
        """Soft delete customer by setting status to deleted."""
        return self.update_customer(customer_id, {'status': 'deleted'})

    # ========================================================================
    # Payment Methods CRUD
    # ========================================================================

    def create_payment_method(self, data: Dict[str, Any]) -> str:
        """Create a new payment method."""
        pm_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        self.backend.execute(f'''
            INSERT INTO payment_methods (
                id, customer_id, type, provider, provider_payment_id,
                last_four, brand, exp_month, exp_year,
                is_default, is_active, billing_name, billing_email,
                created_at, updated_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            pm_id,
            data['customer_id'],
            data['type'],
            data['provider'],
            data.get('provider_payment_id', ''),
            data.get('last_four', ''),
            data.get('brand', ''),
            data.get('exp_month'),
            data.get('exp_year'),
            self._bool_val(data.get('is_default', False)),
            self._bool_val(data.get('is_active', True)),
            data.get('billing_name', ''),
            data.get('billing_email', ''),
            now,
            now
        ))
        self.backend.commit()
        return pm_id

    def get_payment_method(self, pm_id: str) -> Optional[Dict[str, Any]]:
        """Get payment method by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM payment_methods WHERE id = {ph}',
            (pm_id,)
        )
        return dict(row) if row else None

    def get_customer_payment_methods(self, customer_id: str, active_only: bool = True) -> List[Dict[str, Any]]:
        """Get all payment methods for a customer."""
        ph = self.backend.get_placeholder()
        if active_only:
            rows = self.backend.fetchall(
                f'''SELECT * FROM payment_methods
                    WHERE customer_id = {ph} AND is_active = {ph}
                    ORDER BY is_default DESC, created_at DESC''',
                (customer_id, self._bool_val(True))
            )
        else:
            rows = self.backend.fetchall(
                f'''SELECT * FROM payment_methods
                    WHERE customer_id = {ph}
                    ORDER BY is_default DESC, created_at DESC''',
                (customer_id,)
            )
        return [dict(row) for row in rows]

    def set_default_payment_method(self, customer_id: str, pm_id: str) -> bool:
        """Set a payment method as default for a customer."""
        ph = self.backend.get_placeholder()
        # First, unset all defaults for this customer
        self.backend.execute(
            f'UPDATE payment_methods SET is_default = {ph} WHERE customer_id = {ph}',
            (self._bool_val(False), customer_id)
        )
        # Then set the specified one as default
        self.backend.execute(
            f'UPDATE payment_methods SET is_default = {ph} WHERE id = {ph}',
            (self._bool_val(True), pm_id)
        )
        self.backend.commit()
        return True

    def delete_payment_method(self, pm_id: str) -> bool:
        """Soft delete payment method."""
        ph = self.backend.get_placeholder()
        self.backend.execute(
            f'UPDATE payment_methods SET is_active = {ph}, updated_at = {ph} WHERE id = {ph}',
            (self._bool_val(False), self._now(), pm_id)
        )
        self.backend.commit()
        return True

    # ========================================================================
    # Licenses CRUD
    # ========================================================================

    def create_license(self, license_data: Dict[str, Any]) -> str:
        """Create a new license record."""
        license_id = license_data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        custom_data = license_data.get('custom_data', {})
        if isinstance(custom_data, dict):
            custom_data = json.dumps(custom_data)

        self.backend.execute(f'''
            INSERT INTO licenses (
                id, customer_id, plan_id, license_type, name, email, organization,
                max_seats, issue_date, expiry_date, hardware_bound, hardware_fingerprint,
                feature_flags, custom_data, issuer, notes, file_path, status,
                created_at, updated_at,
                -- Version 2 Extended Metadata
                license_version, min_browser_version, max_browser_version,
                allowed_regions, export_control, total_activations, last_device_name,
                order_id, invoice_id, reseller_id, support_tier, support_expiry_date,
                revocation_check_url, issued_ip, maintenance_included, maintenance_expiry_date
            ) VALUES (
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}
            )
        ''', (
            license_id,
            license_data.get('customer_id'),
            license_data.get('plan_id'),
            license_data.get('license_type', 0),
            license_data.get('name', ''),
            license_data.get('email', ''),
            license_data.get('organization', ''),
            license_data.get('max_seats', 1),
            license_data.get('issue_date', now),
            license_data.get('expiry_date'),
            self._bool_val(license_data.get('hardware_bound', False)),
            license_data.get('hardware_fingerprint', ''),
            license_data.get('feature_flags', 0),
            custom_data,
            license_data.get('issuer', 'admin'),
            license_data.get('notes', ''),
            license_data.get('file_path', ''),
            license_data.get('status', 'active'),
            now,
            now,
            # Version 2 Extended Metadata
            license_data.get('license_version', 2),
            license_data.get('min_browser_version', ''),
            license_data.get('max_browser_version', ''),
            license_data.get('allowed_regions', ''),
            license_data.get('export_control', ''),
            license_data.get('total_activations', 0),
            license_data.get('last_device_name', ''),
            license_data.get('order_id', ''),
            license_data.get('invoice_id', ''),
            license_data.get('reseller_id', ''),
            license_data.get('support_tier', 0),
            license_data.get('support_expiry_date'),
            license_data.get('revocation_check_url', ''),
            license_data.get('issued_ip', ''),
            self._bool_val(license_data.get('maintenance_included', False)),
            license_data.get('maintenance_expiry_date')
        ))
        self.backend.commit()
        return license_id

    def get_license(self, license_id: str) -> Optional[Dict[str, Any]]:
        """Get a license by ID with customer and plan info."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'''SELECT l.*,
                       c.name as customer_name, c.company as customer_company, c.email as customer_email,
                       p.display_name as plan_name, p.price_monthly as plan_price
                FROM licenses l
                LEFT JOIN customers c ON l.customer_id = c.id
                LEFT JOIN billing_plans p ON l.plan_id = p.id
                WHERE l.id = {ph}''',
            (license_id,)
        )
        if row:
            result = dict(row)
            if result.get('custom_data') and isinstance(result['custom_data'], str):
                try:
                    result['custom_data'] = json.loads(result['custom_data'])
                except:
                    pass
            return result
        return None

    def get_license_by_email(self, email: str) -> List[Dict[str, Any]]:
        """Get all licenses for an email."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'SELECT * FROM licenses WHERE email = {ph} ORDER BY created_at DESC',
            (email,)
        )
        return [dict(row) for row in rows]

    def get_customer_licenses(self, customer_id: str) -> List[Dict[str, Any]]:
        """Get all licenses for a customer."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'SELECT * FROM licenses WHERE customer_id = {ph} ORDER BY created_at DESC',
            (customer_id,)
        )
        return [dict(row) for row in rows]

    def get_all_licenses(self,
                         status: Optional[str] = None,
                         license_type: Optional[int] = None,
                         customer_id: Optional[str] = None,
                         limit: int = 100,
                         offset: int = 0) -> List[Dict[str, Any]]:
        """Get all licenses with optional filtering."""
        ph = self.backend.get_placeholder()
        conditions = []
        params = []

        if status:
            conditions.append(f'status = {ph}')
            params.append(status)
        if license_type is not None:
            conditions.append(f'license_type = {ph}')
            params.append(license_type)
        if customer_id:
            conditions.append(f'customer_id = {ph}')
            params.append(customer_id)

        where_clause = ' AND '.join(conditions) if conditions else '1=1'

        rows = self.backend.fetchall(
            f'''SELECT * FROM licenses
                WHERE {where_clause}
                ORDER BY created_at DESC
                LIMIT {ph} OFFSET {ph}''',
            tuple(params) + (limit, offset)
        )
        return [dict(row) for row in rows]

    def update_license(self, license_id: str, updates: Dict[str, Any]) -> bool:
        """Update a license."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        if 'custom_data' in updates and isinstance(updates['custom_data'], dict):
            updates['custom_data'] = json.dumps(updates['custom_data'])

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [license_id]

        self.backend.execute(
            f'UPDATE licenses SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def revoke_license(self, license_id: str, reason: str = '') -> bool:
        """Revoke a license."""
        return self.update_license(license_id, {
            'status': 'revoked',
            'notes': f'Revoked: {reason}'
        })

    def delete_license(self, license_id: str) -> bool:
        """Delete a license (soft delete by setting status to deleted)."""
        return self.update_license(license_id, {'status': 'deleted'})

    def get_license_count(self, status: Optional[str] = None) -> int:
        """Get total license count."""
        ph = self.backend.get_placeholder()
        if status:
            row = self.backend.fetchone(
                f'SELECT COUNT(*) as count FROM licenses WHERE status = {ph}',
                (status,)
            )
        else:
            row = self.backend.fetchone('SELECT COUNT(*) as count FROM licenses')
        return row['count'] if row else 0

    # ========================================================================
    # Subscriptions CRUD
    # ========================================================================

    def create_subscription(self, license_id: str, data: Dict[str, Any]) -> str:
        """Create a subscription record for a license."""
        subscription_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        self.backend.execute(f'''
            INSERT INTO subscriptions (
                id, license_id, customer_id, plan_id, status, billing_cycle,
                current_period_start, current_period_end, activation_date,
                last_check_date, next_check_date, grace_period_days, trial_end_date,
                cancel_at_period_end, canceled_at, payment_provider, payment_id,
                stripe_subscription_id, paypal_subscription_id, created_at, updated_at
            ) VALUES (
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}
            )
        ''', (
            subscription_id,
            license_id,
            data.get('customer_id'),
            data.get('plan_id'),
            data.get('status', 'active'),
            data.get('billing_cycle', 'monthly'),
            data.get('current_period_start'),
            data.get('current_period_end'),
            data.get('activation_date'),
            data.get('last_check_date'),
            data.get('next_check_date'),
            data.get('grace_period_days', 7),
            data.get('trial_end_date'),
            self._bool_val(data.get('cancel_at_period_end', False)),
            data.get('canceled_at'),
            data.get('payment_provider', ''),
            data.get('payment_id', ''),
            data.get('stripe_subscription_id', ''),
            data.get('paypal_subscription_id', ''),
            now,
            now
        ))
        self.backend.commit()
        return subscription_id

    def get_subscription(self, license_id: str) -> Optional[Dict[str, Any]]:
        """Get subscription for a license."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM subscriptions WHERE license_id = {ph}',
            (license_id,)
        )
        return dict(row) if row else None

    def get_subscription_by_id(self, subscription_id: str) -> Optional[Dict[str, Any]]:
        """Get subscription by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM subscriptions WHERE id = {ph}',
            (subscription_id,)
        )
        return dict(row) if row else None

    def get_customer_subscriptions(self, customer_id: str) -> List[Dict[str, Any]]:
        """Get all subscriptions for a customer."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT s.*, l.name, l.email, l.license_type
                FROM subscriptions s
                JOIN licenses l ON s.license_id = l.id
                WHERE s.customer_id = {ph}
                ORDER BY s.created_at DESC''',
            (customer_id,)
        )
        return [dict(row) for row in rows]

    def update_subscription(self, license_id: str, updates: Dict[str, Any]) -> bool:
        """Update subscription status."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [license_id]

        self.backend.execute(
            f'UPDATE subscriptions SET {", ".join(set_clauses)} WHERE license_id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def cancel_subscription(self, license_id: str, at_period_end: bool = True) -> bool:
        """Cancel a subscription."""
        now = self._now()
        if at_period_end:
            return self.update_subscription(license_id, {
                'cancel_at_period_end': 1,
            })
        else:
            return self.update_subscription(license_id, {
                'status': 'canceled',
                'canceled_at': now
            })

    def get_active_subscriptions(self) -> List[Dict[str, Any]]:
        """Get all active subscriptions with license info."""
        rows = self.backend.fetchall('''
            SELECT s.*, l.name, l.email, l.organization, l.expiry_date, l.license_type,
                   c.name as customer_name, c.company as customer_company, c.email as customer_email,
                   p.display_name as plan_name, p.price_monthly as plan_price
            FROM subscriptions s
            JOIN licenses l ON s.license_id = l.id
            LEFT JOIN customers c ON s.customer_id = c.id
            LEFT JOIN billing_plans p ON s.plan_id = p.id
            WHERE s.status = 'active'
            ORDER BY s.next_check_date ASC
        ''')
        return [dict(row) for row in rows]

    def get_expiring_subscriptions(self, days: int = 7) -> List[Dict[str, Any]]:
        """Get subscriptions expiring within the specified days."""
        ph = self.backend.get_placeholder()
        cutoff = (datetime.now(UTC) + timedelta(days=days)).isoformat()
        rows = self.backend.fetchall(f'''
            SELECT s.*, l.name, l.email, l.license_type,
                   c.name as customer_name, c.email as customer_email
            FROM subscriptions s
            JOIN licenses l ON s.license_id = l.id
            LEFT JOIN customers c ON s.customer_id = c.id
            WHERE s.status = 'active'
              AND s.current_period_end IS NOT NULL
              AND s.current_period_end <= {ph}
            ORDER BY s.current_period_end ASC
        ''', (cutoff,))
        return [dict(row) for row in rows]

    # ========================================================================
    # Invoices CRUD
    # ========================================================================

    def generate_invoice_number(self) -> str:
        """Generate a unique invoice number."""
        # Get prefix from settings
        prefix = self.get_setting('invoice_prefix') or 'OWL-'
        # Get current year/month
        now = datetime.now(UTC)
        year_month = now.strftime('%Y%m')
        # Count invoices this month
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f"SELECT COUNT(*) as count FROM invoices WHERE invoice_number LIKE {ph}",
            (f'{prefix}{year_month}%',)
        )
        count = (row['count'] if row else 0) + 1
        return f"{prefix}{year_month}-{count:04d}"

    def create_invoice(self, data: Dict[str, Any]) -> str:
        """Create a new invoice."""
        invoice_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        invoice_number = data.get('invoice_number', self.generate_invoice_number())

        self.backend.execute(f'''
            INSERT INTO invoices (
                id, invoice_number, customer_id, subscription_id, license_id,
                status, currency, subtotal, tax_amount, tax_rate,
                discount_amount, discount_code, total, amount_paid, amount_due,
                issue_date, due_date, paid_at, payment_method_id,
                payment_provider, payment_provider_id, notes, pdf_path,
                created_at, updated_at
            ) VALUES (
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}, {ph}
            )
        ''', (
            invoice_id,
            invoice_number,
            data['customer_id'],
            data.get('subscription_id'),
            data.get('license_id'),
            data.get('status', 'draft'),
            data.get('currency', 'USD'),
            data.get('subtotal', 0),
            data.get('tax_amount', 0),
            data.get('tax_rate', 0),
            data.get('discount_amount', 0),
            data.get('discount_code', ''),
            data.get('total', 0),
            data.get('amount_paid', 0),
            data.get('amount_due', 0),
            data.get('issue_date', now),
            data.get('due_date'),
            data.get('paid_at'),
            data.get('payment_method_id'),
            data.get('payment_provider', ''),
            data.get('payment_provider_id', ''),
            data.get('notes', ''),
            data.get('pdf_path', ''),
            now,
            now
        ))
        self.backend.commit()
        return invoice_id

    def get_invoice(self, invoice_id: str) -> Optional[Dict[str, Any]]:
        """Get invoice by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM invoices WHERE id = {ph}',
            (invoice_id,)
        )
        return dict(row) if row else None

    def get_invoice_by_number(self, invoice_number: str) -> Optional[Dict[str, Any]]:
        """Get invoice by invoice number."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM invoices WHERE invoice_number = {ph}',
            (invoice_number,)
        )
        return dict(row) if row else None

    def get_customer_invoices(self, customer_id: str, limit: int = 50) -> List[Dict[str, Any]]:
        """Get all invoices for a customer."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT * FROM invoices
                WHERE customer_id = {ph}
                ORDER BY issue_date DESC
                LIMIT {ph}''',
            (customer_id, limit)
        )
        return [dict(row) for row in rows]

    def get_all_invoices(self,
                         status: Optional[str] = None,
                         customer_id: Optional[str] = None,
                         limit: int = 100,
                         offset: int = 0) -> List[Dict[str, Any]]:
        """Get all invoices with optional filtering."""
        ph = self.backend.get_placeholder()
        conditions = []
        params = []

        if status:
            conditions.append(f'i.status = {ph}')
            params.append(status)
        if customer_id:
            conditions.append(f'i.customer_id = {ph}')
            params.append(customer_id)

        where_clause = ' AND '.join(conditions) if conditions else '1=1'

        rows = self.backend.fetchall(
            f'''SELECT i.*, c.name as customer_name, c.email as customer_email
                FROM invoices i
                LEFT JOIN customers c ON i.customer_id = c.id
                WHERE {where_clause}
                ORDER BY i.issue_date DESC
                LIMIT {ph} OFFSET {ph}''',
            tuple(params) + (limit, offset)
        )
        return [dict(row) for row in rows]

    def update_invoice(self, invoice_id: str, updates: Dict[str, Any]) -> bool:
        """Update invoice."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [invoice_id]

        self.backend.execute(
            f'UPDATE invoices SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def mark_invoice_paid(self, invoice_id: str, payment_method: str = None, payment_ref: str = None) -> bool:
        """Mark invoice as paid."""
        invoice = self.get_invoice(invoice_id)
        updates = {
            'status': 'paid',
            'paid_at': self._now(),
        }
        if invoice:
            updates['amount_paid'] = invoice['total']
            updates['amount_due'] = 0
        if payment_method:
            updates['payment_method'] = payment_method
        if payment_ref:
            updates['payment_reference'] = payment_ref
        return self.update_invoice(invoice_id, updates)

    def void_invoice(self, invoice_id: str) -> bool:
        """Void an invoice."""
        return self.update_invoice(invoice_id, {'status': 'voided'})

    def get_invoice_count(self, status: Optional[str] = None) -> int:
        """Get count of invoices."""
        ph = self.backend.get_placeholder()
        if status:
            row = self.backend.fetchone(
                f'SELECT COUNT(*) as count FROM invoices WHERE status = {ph}',
                (status,)
            )
        else:
            row = self.backend.fetchone('SELECT COUNT(*) as count FROM invoices')
        return row['count'] if row else 0

    # ========================================================================
    # Invoice Items CRUD
    # ========================================================================

    def create_invoice_item(self, data: Dict[str, Any]) -> str:
        """Create a new invoice line item."""
        item_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        self.backend.execute(f'''
            INSERT INTO invoice_items (
                id, invoice_id, description, quantity, unit_price, amount,
                plan_id, license_id, period_start, period_end, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            item_id,
            data['invoice_id'],
            data['description'],
            data.get('quantity', 1),
            data['unit_price'],
            data['amount'],
            data.get('plan_id'),
            data.get('license_id'),
            data.get('period_start'),
            data.get('period_end'),
            now
        ))
        self.backend.commit()
        return item_id

    def get_invoice_items(self, invoice_id: str) -> List[Dict[str, Any]]:
        """Get all line items for an invoice."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'SELECT * FROM invoice_items WHERE invoice_id = {ph} ORDER BY created_at',
            (invoice_id,)
        )
        return [dict(row) for row in rows]

    # ========================================================================
    # Billing History CRUD
    # ========================================================================

    def create_billing_history(self, data: Dict[str, Any]) -> str:
        """Create a billing history record (payment transaction)."""
        history_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        metadata = data.get('metadata', {})
        if isinstance(metadata, dict):
            metadata = json.dumps(metadata)

        self.backend.execute(f'''
            INSERT INTO billing_history (
                id, customer_id, invoice_id, subscription_id, type, status,
                amount, currency, payment_method_id, payment_provider,
                payment_provider_id, failure_reason, refund_reason,
                refunded_amount, metadata, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            history_id,
            data['customer_id'],
            data.get('invoice_id'),
            data.get('subscription_id'),
            data['type'],  # payment, refund, chargeback, etc.
            data['status'],  # succeeded, failed, pending, etc.
            data['amount'],
            data.get('currency', 'USD'),
            data.get('payment_method_id'),
            data.get('payment_provider', ''),
            data.get('payment_provider_id', ''),
            data.get('failure_reason', ''),
            data.get('refund_reason', ''),
            data.get('refunded_amount', 0),
            metadata,
            now
        ))
        self.backend.commit()
        return history_id

    def get_customer_billing_history(self, customer_id: str, limit: int = 50) -> List[Dict[str, Any]]:
        """Get billing history for a customer."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT * FROM billing_history
                WHERE customer_id = {ph}
                ORDER BY created_at DESC
                LIMIT {ph}''',
            (customer_id, limit)
        )
        return [dict(row) for row in rows]

    def get_all_billing_history(self,
                                 type_filter: Optional[str] = None,
                                 status_filter: Optional[str] = None,
                                 limit: int = 100,
                                 offset: int = 0) -> List[Dict[str, Any]]:
        """Get all billing history with optional filtering."""
        ph = self.backend.get_placeholder()
        conditions = []
        params = []

        if type_filter:
            conditions.append(f'bh.type = {ph}')
            params.append(type_filter)
        if status_filter:
            conditions.append(f'bh.status = {ph}')
            params.append(status_filter)

        where_clause = ' AND '.join(conditions) if conditions else '1=1'

        rows = self.backend.fetchall(
            f'''SELECT bh.*, c.name as customer_name, c.email as customer_email
                FROM billing_history bh
                LEFT JOIN customers c ON bh.customer_id = c.id
                WHERE {where_clause}
                ORDER BY bh.created_at DESC
                LIMIT {ph} OFFSET {ph}''',
            tuple(params) + (limit, offset)
        )
        return [dict(row) for row in rows]

    def get_billing_history(self, limit: int = 100, offset: int = 0) -> List[Dict[str, Any]]:
        """Alias for get_all_billing_history."""
        return self.get_all_billing_history(limit=limit, offset=offset)

    def get_billing_history_count(self) -> int:
        """Get total count of billing history records."""
        row = self.backend.fetchone('SELECT COUNT(*) as count FROM billing_history')
        return row['count'] if row else 0

    # ========================================================================
    # Discount Codes CRUD
    # ========================================================================

    def create_discount_code(self, data: Dict[str, Any]) -> str:
        """Create a new discount code."""
        code_id = data.get('id', self._generate_id())
        now = self._now()
        ph = self.backend.get_placeholder()

        plan_ids = data.get('plan_ids', [])
        if isinstance(plan_ids, list):
            plan_ids = ','.join(plan_ids)

        self.backend.execute(f'''
            INSERT INTO discount_codes (
                id, code, description, discount_type, discount_value, currency,
                max_uses, times_used, min_amount, applies_to, plan_ids,
                valid_from, valid_until, is_active, created_by, created_at, updated_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            code_id,
            data['code'].upper(),
            data.get('description', ''),
            data['discount_type'],  # 'percentage' or 'fixed'
            data['discount_value'],
            data.get('currency', 'USD'),
            data.get('max_uses'),
            0,
            data.get('min_amount', 0),
            data.get('applies_to', 'all'),  # all, subscription, one-time
            plan_ids,
            data.get('valid_from'),
            data.get('valid_until'),
            self._bool_val(data.get('is_active', True)),
            data.get('created_by', ''),
            now,
            now
        ))
        self.backend.commit()
        return code_id

    def get_discount_code(self, code: str) -> Optional[Dict[str, Any]]:
        """Get discount code by code string."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM discount_codes WHERE code = {ph}',
            (code.upper(),)
        )
        return dict(row) if row else None

    def validate_discount_code(self, code: str, amount: float = 0, plan_id: str = None) -> Tuple[bool, str, float]:
        """
        Validate a discount code and return discount amount.
        Returns: (is_valid, error_message, discount_amount)
        """
        discount = self.get_discount_code(code)
        if not discount:
            return False, "Invalid discount code", 0

        if not discount['is_active']:
            return False, "Discount code is inactive", 0

        if discount['max_uses'] and discount['times_used'] >= discount['max_uses']:
            return False, "Discount code has reached maximum uses", 0

        now = datetime.now(UTC).isoformat()
        if discount['valid_from'] and discount['valid_from'] > now:
            return False, "Discount code is not yet valid", 0
        if discount['valid_until'] and discount['valid_until'] < now:
            return False, "Discount code has expired", 0

        if discount['min_amount'] and amount < discount['min_amount']:
            return False, f"Minimum amount of {discount['min_amount']} required", 0

        if plan_id and discount['plan_ids']:
            valid_plans = discount['plan_ids'].split(',')
            if plan_id not in valid_plans:
                return False, "Discount code not valid for this plan", 0

        # Calculate discount
        if discount['discount_type'] == 'percentage':
            discount_amount = amount * (discount['discount_value'] / 100)
        else:
            discount_amount = min(discount['discount_value'], amount)

        return True, "", discount_amount

    def use_discount_code(self, code: str) -> bool:
        """Increment the times_used counter for a discount code."""
        ph = self.backend.get_placeholder()
        self.backend.execute(
            f'UPDATE discount_codes SET times_used = times_used + 1, updated_at = {ph} WHERE code = {ph}',
            (self._now(), code.upper())
        )
        self.backend.commit()
        return True

    def get_all_discount_codes(self, active_only: bool = False) -> List[Dict[str, Any]]:
        """Get all discount codes."""
        ph = self.backend.get_placeholder()
        if active_only:
            rows = self.backend.fetchall(
                f'SELECT * FROM discount_codes WHERE is_active = {ph} ORDER BY created_at DESC',
                (self._bool_val(True),)
            )
        else:
            rows = self.backend.fetchall(
                'SELECT * FROM discount_codes ORDER BY created_at DESC'
            )
        return [dict(row) for row in rows]

    def get_discount_code_by_code(self, code: str) -> Optional[Dict[str, Any]]:
        """Get discount code by code string."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM discount_codes WHERE code = {ph}',
            (code.upper(),)
        )
        return dict(row) if row else None

    def update_discount_code(self, code_id: str, updates: Dict[str, Any]) -> bool:
        """Update a discount code."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [code_id]

        self.backend.execute(
            f'UPDATE discount_codes SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def delete_discount_code(self, code_id: str) -> bool:
        """Delete a discount code."""
        ph = self.backend.get_placeholder()
        self.backend.execute(
            f'DELETE FROM discount_codes WHERE id = {ph}',
            (code_id,)
        )
        self.backend.commit()
        return True

    # ========================================================================
    # Activation Logs
    # ========================================================================

    def log_activation(self, license_id: str, action: str,
                      hardware_fingerprint: str = '',
                      ip_address: str = '',
                      user_agent: str = '',
                      success: bool = True,
                      error_message: str = '',
                      customer_id: str = None):
        """Log an activation attempt."""
        ph = self.backend.get_placeholder()
        self.backend.execute(f'''
            INSERT INTO activation_logs (
                license_id, customer_id, action, hardware_fingerprint, ip_address,
                user_agent, success, error_message, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            license_id, customer_id, action, hardware_fingerprint, ip_address,
            user_agent, self._bool_val(success), error_message,
            self._now()
        ))
        self.backend.commit()

    def get_activation_logs(self, license_id: str, limit: int = 50) -> List[Dict[str, Any]]:
        """Get activation logs for a license."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT * FROM activation_logs
                WHERE license_id = {ph}
                ORDER BY created_at DESC
                LIMIT {ph}''',
            (license_id, limit)
        )
        return [dict(row) for row in rows]

    def get_recent_activations_with_details(self, limit: int = 50) -> List[Dict[str, Any]]:
        """Get recent activation logs with license details."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT a.*, l.name, l.email, l.organization, l.license_type,
                       c.name as customer_name
                FROM activation_logs a
                JOIN licenses l ON a.license_id = l.id
                LEFT JOIN customers c ON a.customer_id = c.id
                ORDER BY a.created_at DESC
                LIMIT {ph}''',
            (limit,)
        )
        return [dict(row) for row in rows]

    # ========================================================================
    # Nonce Management
    # ========================================================================

    def use_nonce(self, nonce: str, license_id: str) -> bool:
        """Mark a nonce as used. Returns False if already used."""
        ph = self.backend.get_placeholder()
        try:
            self.backend.execute(
                f'INSERT INTO used_nonces (nonce, license_id, used_at) VALUES ({ph}, {ph}, {ph})',
                (nonce, license_id, self._now())
            )
            self.backend.commit()
            return True
        except Exception:
            return False

    def cleanup_old_nonces(self, days: int = 30):
        """Remove nonces older than specified days."""
        ph = self.backend.get_placeholder()
        cutoff = (datetime.now(UTC) - timedelta(days=days)).isoformat()
        self.backend.execute(
            f'DELETE FROM used_nonces WHERE used_at < {ph}',
            (cutoff,)
        )
        self.backend.commit()

    # ========================================================================
    # Admin Audit Log
    # ========================================================================

    def log_admin_action(self, admin_user: str, action: str,
                        target_type: str = '', target_id: str = '',
                        details: str = '', ip_address: str = '',
                        user_agent: str = '', admin_user_id: str = None):
        """Log an admin action."""
        ph = self.backend.get_placeholder()
        self.backend.execute(f'''
            INSERT INTO admin_audit_log (
                admin_user_id, admin_user, action, target_type, target_id,
                details, ip_address, user_agent, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            admin_user_id, admin_user, action, target_type, target_id,
            details, ip_address, user_agent, self._now()
        ))
        self.backend.commit()

    def get_admin_audit_log(self, limit: int = 100,
                            admin_user: str = None,
                            action: str = None) -> List[Dict[str, Any]]:
        """Get admin audit log."""
        ph = self.backend.get_placeholder()
        conditions = []
        params = []

        if admin_user:
            conditions.append(f'admin_user = {ph}')
            params.append(admin_user)
        if action:
            conditions.append(f'action = {ph}')
            params.append(action)

        where_clause = ' AND '.join(conditions) if conditions else '1=1'

        rows = self.backend.fetchall(
            f'''SELECT * FROM admin_audit_log
                WHERE {where_clause}
                ORDER BY created_at DESC
                LIMIT {ph}''',
            tuple(params) + (limit,)
        )
        return [dict(row) for row in rows]

    # ========================================================================
    # License Seats
    # ========================================================================

    def activate_seat(self, license_id: str, hardware_fingerprint: str,
                     ip_address: str = '', user_agent: str = '',
                     device_name: str = '', os_info: str = '',
                     browser_version: str = '', customer_id: str = None) -> Tuple[bool, str, int]:
        """
        Activate a seat for a license.

        Returns: (success, error_message, current_seat_count)
        """
        ph = self.backend.get_placeholder()
        now = self._now()

        # Get license info for max_seats
        license_data = self.get_license(license_id)
        if not license_data:
            return False, "License not found", 0

        max_seats = license_data.get('max_seats', 1)

        # Check if this hardware fingerprint already has a seat
        existing = self.backend.fetchone(
            f'''SELECT * FROM license_seats
                WHERE license_id = {ph} AND hardware_fingerprint = {ph}''',
            (license_id, hardware_fingerprint)
        )

        if existing:
            existing = dict(existing)
            # Update last_seen_at for existing seat
            if existing['is_active']:
                self.backend.execute(
                    f'''UPDATE license_seats SET last_seen_at = {ph}, ip_address = {ph}, user_agent = {ph},
                        device_name = {ph}, os_info = {ph}, browser_version = {ph}
                        WHERE license_id = {ph} AND hardware_fingerprint = {ph}''',
                    (now, ip_address, user_agent, device_name, os_info, browser_version, license_id, hardware_fingerprint)
                )
                self.backend.commit()
                current_count = self.get_active_seat_count(license_id)
                return True, "", current_count
            else:
                # Reactivate the seat
                current_count = self.get_active_seat_count(license_id)
                if current_count >= max_seats:
                    return False, f"Maximum seats ({max_seats}) reached", current_count

                self.backend.execute(
                    f'''UPDATE license_seats SET is_active = {ph}, last_seen_at = {ph},
                        ip_address = {ph}, user_agent = {ph}, device_name = {ph}, os_info = {ph}, browser_version = {ph},
                        deactivated_at = NULL
                        WHERE license_id = {ph} AND hardware_fingerprint = {ph}''',
                    (self._bool_val(True), now, ip_address, user_agent, device_name, os_info, browser_version,
                     license_id, hardware_fingerprint)
                )
                self.backend.commit()
                return True, "", current_count + 1

        # Check if adding a new seat would exceed max_seats
        current_count = self.get_active_seat_count(license_id)
        if current_count >= max_seats:
            return False, f"Maximum seats ({max_seats}) reached", current_count

        # Create new seat
        try:
            self.backend.execute(f'''
                INSERT INTO license_seats (
                    license_id, customer_id, hardware_fingerprint, device_name, os_info,
                    browser_version, first_activated_at, last_seen_at, ip_address, user_agent, is_active
                ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
            ''', (license_id, customer_id, hardware_fingerprint, device_name, os_info,
                  browser_version, now, now, ip_address, user_agent, self._bool_val(True)))
            self.backend.commit()
            return True, "", current_count + 1
        except Exception as e:
            return False, f"Failed to create seat: {str(e)}", current_count

    def deactivate_seat(self, license_id: str, hardware_fingerprint: str,
                       reason: str = '') -> bool:
        """Deactivate a specific seat."""
        ph = self.backend.get_placeholder()
        now = self._now()

        self.backend.execute(
            f'''UPDATE license_seats SET is_active = {ph}, deactivated_at = {ph}, deactivated_reason = {ph}
                WHERE license_id = {ph} AND hardware_fingerprint = {ph}''',
            (self._bool_val(False), now, reason, license_id, hardware_fingerprint)
        )
        self.backend.commit()
        return True

    def get_active_seat_count(self, license_id: str) -> int:
        """Get the count of active seats for a license."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'''SELECT COUNT(*) as count FROM license_seats
                WHERE license_id = {ph} AND is_active = {ph}''',
            (license_id, self._bool_val(True))
        )
        return row['count'] if row else 0

    def get_license_seats(self, license_id: str) -> List[Dict[str, Any]]:
        """Get all seats for a license."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT * FROM license_seats
                WHERE license_id = {ph}
                ORDER BY first_activated_at DESC''',
            (license_id,)
        )
        return [dict(row) for row in rows]

    def get_all_active_seats(self, limit: int = 100) -> List[Dict[str, Any]]:
        """Get all active seats across all licenses with license info."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT s.*, l.name, l.email, l.organization, l.max_seats, l.license_type,
                       c.name as customer_name
                FROM license_seats s
                JOIN licenses l ON s.license_id = l.id
                LEFT JOIN customers c ON s.customer_id = c.id
                WHERE s.is_active = {ph}
                ORDER BY s.last_seen_at DESC
                LIMIT {ph}''',
            (self._bool_val(True), limit)
        )
        return [dict(row) for row in rows]

    def clear_inactive_seats(self, license_id: str = None) -> int:
        """
        Delete all inactive (deactivated) seats.
        If license_id is provided, only clear inactive seats for that license.
        Returns the number of seats deleted.
        """
        ph = self.backend.get_placeholder()

        # Get count first
        if license_id:
            count_row = self.backend.fetchone(
                f'''SELECT COUNT(*) as count FROM license_seats
                    WHERE is_active = {ph} AND license_id = {ph}''',
                (self._bool_val(False), license_id)
            )
        else:
            count_row = self.backend.fetchone(
                f'''SELECT COUNT(*) as count FROM license_seats
                    WHERE is_active = {ph}''',
                (self._bool_val(False),)
            )

        count = count_row['count'] if count_row else 0

        # Delete inactive seats
        if license_id:
            self.backend.execute(
                f'''DELETE FROM license_seats
                    WHERE is_active = {ph} AND license_id = {ph}''',
                (self._bool_val(False), license_id)
            )
        else:
            self.backend.execute(
                f'''DELETE FROM license_seats WHERE is_active = {ph}''',
                (self._bool_val(False),)
            )

        self.backend.commit()
        return count

    def clear_activation_logs(self, license_id: str = None) -> int:
        """
        Delete all activation logs.
        If license_id is provided, only clear logs for that license.
        Returns the number of logs deleted.
        """
        ph = self.backend.get_placeholder()

        # Get count first
        if license_id:
            count_row = self.backend.fetchone(
                f'''SELECT COUNT(*) as count FROM activation_logs
                    WHERE license_id = {ph}''',
                (license_id,)
            )
        else:
            count_row = self.backend.fetchone(
                'SELECT COUNT(*) as count FROM activation_logs', ()
            )

        count = count_row['count'] if count_row else 0

        # Delete logs
        if license_id:
            self.backend.execute(
                f'''DELETE FROM activation_logs WHERE license_id = {ph}''',
                (license_id,)
            )
        else:
            self.backend.execute('DELETE FROM activation_logs', ())

        self.backend.commit()
        return count

    def get_inactive_seat_count(self, license_id: str = None) -> int:
        """Get count of inactive seats, optionally for a specific license."""
        ph = self.backend.get_placeholder()
        if license_id:
            row = self.backend.fetchone(
                f'''SELECT COUNT(*) as count FROM license_seats
                    WHERE is_active = {ph} AND license_id = {ph}''',
                (self._bool_val(False), license_id)
            )
        else:
            row = self.backend.fetchone(
                f'''SELECT COUNT(*) as count FROM license_seats
                    WHERE is_active = {ph}''',
                (self._bool_val(False),)
            )
        return row['count'] if row else 0

    def get_activation_log_count(self, license_id: str = None) -> int:
        """Get total count of activation logs, optionally for a specific license."""
        ph = self.backend.get_placeholder()
        if license_id:
            row = self.backend.fetchone(
                f'''SELECT COUNT(*) as count FROM activation_logs
                    WHERE license_id = {ph}''',
                (license_id,)
            )
        else:
            row = self.backend.fetchone(
                'SELECT COUNT(*) as count FROM activation_logs', ()
            )
        return row['count'] if row else 0

    # ========================================================================
    # Customer Activity Log
    # ========================================================================

    def log_customer_activity(self, customer_id: str, activity_type: str,
                              description: str = '', license_id: str = None,
                              ip_address: str = '', user_agent: str = '',
                              metadata: Dict = None):
        """Log a customer activity."""
        ph = self.backend.get_placeholder()
        meta_json = json.dumps(metadata) if metadata else None

        self.backend.execute(f'''
            INSERT INTO customer_activity_log (
                customer_id, license_id, activity_type, description,
                ip_address, user_agent, metadata, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            customer_id, license_id, activity_type, description,
            ip_address, user_agent, meta_json, self._now()
        ))
        self.backend.commit()

    def get_customer_activity(self, customer_id: str, limit: int = 50) -> List[Dict[str, Any]]:
        """Get activity log for a customer."""
        ph = self.backend.get_placeholder()
        rows = self.backend.fetchall(
            f'''SELECT * FROM customer_activity_log
                WHERE customer_id = {ph}
                ORDER BY created_at DESC
                LIMIT {ph}''',
            (customer_id, limit)
        )
        return [dict(row) for row in rows]

    # ========================================================================
    # Email Templates
    # ========================================================================

    def get_email_template(self, name: str) -> Optional[Dict[str, Any]]:
        """Get email template by name."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM email_templates WHERE name = {ph} AND is_active = {ph}',
            (name, self._bool_val(True))
        )
        return dict(row) if row else None

    def get_email_template_by_id(self, template_id: str) -> Optional[Dict[str, Any]]:
        """Get email template by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM email_templates WHERE id = {ph}',
            (template_id,)
        )
        return dict(row) if row else None

    def get_all_email_templates(self) -> List[Dict[str, Any]]:
        """Get all email templates."""
        rows = self.backend.fetchall(
            'SELECT * FROM email_templates ORDER BY name'
        )
        return [dict(row) for row in rows]

    def update_email_template(self, template_id: str, updates: Dict[str, Any]) -> bool:
        """Update email template."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [template_id]

        self.backend.execute(
            f'UPDATE email_templates SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    # ========================================================================
    # Email Log
    # ========================================================================

    def log_email(self, data: Dict[str, Any]) -> int:
        """Log an email send attempt."""
        ph = self.backend.get_placeholder()
        self.backend.execute(f'''
            INSERT INTO email_log (
                customer_id, template_id, to_email, subject, status,
                provider, provider_message_id, error_message, sent_at, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            data.get('customer_id'),
            data.get('template_id'),
            data['to_email'],
            data['subject'],
            data.get('status', 'pending'),
            data.get('provider', ''),
            data.get('provider_message_id', ''),
            data.get('error_message', ''),
            data.get('sent_at'),
            self._now()
        ))
        self.backend.commit()
        return self.backend.lastrowid() if hasattr(self.backend, 'lastrowid') else 0

    def update_email_status(self, email_id: int, status: str,
                            error_message: str = None) -> bool:
        """Update email status."""
        ph = self.backend.get_placeholder()
        if error_message:
            self.backend.execute(
                f'UPDATE email_log SET status = {ph}, error_message = {ph} WHERE id = {ph}',
                (status, error_message, email_id)
            )
        else:
            self.backend.execute(
                f'UPDATE email_log SET status = {ph} WHERE id = {ph}',
                (status, email_id)
            )
        self.backend.commit()
        return True

    # ========================================================================
    # Settings
    # ========================================================================

    def get_setting(self, key: str) -> Optional[str]:
        """Get a setting value."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT value, type FROM settings WHERE key = {ph}',
            (key,)
        )
        if row:
            value = row['value']
            value_type = row['type']
            if value_type == 'integer':
                return int(value) if value else None
            elif value_type == 'float':
                return float(value) if value else None
            elif value_type == 'boolean':
                return value.lower() in ('true', '1', 'yes') if value else False
            elif value_type == 'json':
                return json.loads(value) if value else None
            return value
        return None

    def set_setting(self, key: str, value: Any, value_type: str = 'string',
                   description: str = None) -> bool:
        """Set a setting value (insert or update)."""
        ph = self.backend.get_placeholder()
        now = self._now()

        # Convert value to string for storage
        if value_type == 'json' and not isinstance(value, str):
            value = json.dumps(value)
        elif value is not None:
            value = str(value)

        # Try update first
        existing = self.backend.fetchone(
            f'SELECT key FROM settings WHERE key = {ph}',
            (key,)
        )

        if existing:
            if description:
                self.backend.execute(
                    f'UPDATE settings SET value = {ph}, type = {ph}, description = {ph}, updated_at = {ph} WHERE key = {ph}',
                    (value, value_type, description, now, key)
                )
            else:
                self.backend.execute(
                    f'UPDATE settings SET value = {ph}, type = {ph}, updated_at = {ph} WHERE key = {ph}',
                    (value, value_type, now, key)
                )
        else:
            self.backend.execute(
                f'INSERT INTO settings (key, value, type, description, updated_at) VALUES ({ph}, {ph}, {ph}, {ph}, {ph})',
                (key, value, value_type, description or '', now)
            )

        self.backend.commit()
        return True

    def get_all_settings(self) -> Dict[str, Any]:
        """Get all settings as a dictionary."""
        rows = self.backend.fetchall('SELECT * FROM settings')
        result = {}
        for row in rows:
            row = dict(row)
            value = row['value']
            value_type = row['type']
            if value_type == 'integer':
                value = int(value) if value else None
            elif value_type == 'float':
                value = float(value) if value else None
            elif value_type == 'boolean':
                value = value.lower() in ('true', '1', 'yes') if value else False
            elif value_type == 'json':
                value = json.loads(value) if value else None
            result[row['key']] = value
        return result

    # ========================================================================
    # Customer Users (Portal Login)
    # ========================================================================

    def create_customer_user(self, data: Dict[str, Any]) -> str:
        """Create a new customer portal user."""
        ph = self.backend.get_placeholder()
        user_id = data.get('id', str(uuid.uuid4()))
        now = self._now()

        self.backend.execute(f'''
            INSERT INTO customer_users (
                id, license_id, email, password_hash, customer_id,
                is_active, email_verified, email_verification_token,
                email_verification_expires, created_at, updated_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            user_id,
            data['license_id'],
            data['email'],
            data['password_hash'],
            data.get('customer_id'),
            self._bool_val(data.get('is_active', True)),
            self._bool_val(data.get('email_verified', False)),
            data.get('email_verification_token'),
            data.get('email_verification_expires'),
            now,
            now
        ))
        self.backend.commit()
        return user_id

    def get_customer_user(self, user_id: str) -> Optional[Dict[str, Any]]:
        """Get customer user by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customer_users WHERE id = {ph}',
            (user_id,)
        )
        return dict(row) if row else None

    def get_customer_user_by_email(self, email: str) -> Optional[Dict[str, Any]]:
        """Get customer user by email."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customer_users WHERE email = {ph}',
            (email,)
        )
        return dict(row) if row else None

    def get_customer_user_by_license(self, license_id: str) -> Optional[Dict[str, Any]]:
        """Get customer user by license ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customer_users WHERE license_id = {ph}',
            (license_id,)
        )
        return dict(row) if row else None

    def get_customer_user_by_verification_token(self, token: str) -> Optional[Dict[str, Any]]:
        """Get customer user by email verification token."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customer_users WHERE email_verification_token = {ph}',
            (token,)
        )
        return dict(row) if row else None

    def get_customer_user_by_reset_token(self, token: str) -> Optional[Dict[str, Any]]:
        """Get customer user by password reset token."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customer_users WHERE password_reset_token = {ph}',
            (token,)
        )
        return dict(row) if row else None

    def update_customer_user(self, user_id: str, updates: Dict[str, Any]) -> bool:
        """Update customer user."""
        ph = self.backend.get_placeholder()
        updates['updated_at'] = self._now()

        set_clauses = [f'{key} = {ph}' for key in updates.keys()]
        values = list(updates.values()) + [user_id]

        self.backend.execute(
            f'UPDATE customer_users SET {", ".join(set_clauses)} WHERE id = {ph}',
            tuple(values)
        )
        self.backend.commit()
        return True

    def record_customer_login(self, user_id: str, ip_address: str = None) -> bool:
        """Record customer login timestamp and increment counter."""
        ph = self.backend.get_placeholder()
        now = self._now()
        self.backend.execute(
            f'''UPDATE customer_users
                SET last_login_at = {ph}, last_login_ip = {ph},
                    login_count = login_count + 1, updated_at = {ph}
                WHERE id = {ph}''',
            (now, ip_address, now, user_id)
        )
        self.backend.commit()
        return True

    def verify_license_for_registration(self, license_id: str, email: str) -> Tuple[bool, str, Optional[Dict]]:
        """
        Verify a license can be used for registration.
        Returns (success, error_message, license_data)
        """
        # Check if license exists
        license_data = self.get_license(license_id)
        if not license_data:
            return False, 'License not found', None

        # Check if license email matches
        if license_data['email'].lower() != email.lower():
            return False, 'Email does not match the license', None

        # Check if license is active
        if license_data['status'] != 'active':
            return False, f"License is {license_data['status']}", None

        # Check if a user already exists for this license
        existing_user = self.get_customer_user_by_license(license_id)
        if existing_user:
            return False, 'An account already exists for this license', None

        return True, '', license_data

    # ========================================================================
    # Customer Sessions
    # ========================================================================

    def create_customer_session(self, user_id: str, token_hash: str,
                                 ip_address: str = None, user_agent: str = None,
                                 expires_hours: int = 24) -> str:
        """Create a new customer session."""
        ph = self.backend.get_placeholder()
        session_id = str(uuid.uuid4())
        now = datetime.now(UTC)
        expires_at = (now + timedelta(hours=expires_hours)).isoformat()

        self.backend.execute(f'''
            INSERT INTO customer_sessions (
                id, user_id, token_hash, ip_address, user_agent, expires_at, created_at
            ) VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph}, {ph})
        ''', (
            session_id,
            user_id,
            token_hash,
            ip_address,
            user_agent,
            expires_at,
            now.isoformat()
        ))
        self.backend.commit()
        return session_id

    def get_customer_session(self, session_id: str) -> Optional[Dict[str, Any]]:
        """Get customer session by ID."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'SELECT * FROM customer_sessions WHERE id = {ph}',
            (session_id,)
        )
        return dict(row) if row else None

    def get_customer_session_by_token(self, token_hash: str) -> Optional[Dict[str, Any]]:
        """Get customer session by token hash."""
        ph = self.backend.get_placeholder()
        now = datetime.now(UTC).isoformat()
        row = self.backend.fetchone(
            f'''SELECT s.*, u.email, u.license_id, u.customer_id, u.is_active as user_active
                FROM customer_sessions s
                JOIN customer_users u ON s.user_id = u.id
                WHERE s.token_hash = {ph} AND s.expires_at > {ph}''',
            (token_hash, now)
        )
        return dict(row) if row else None

    def delete_customer_session(self, session_id: str) -> bool:
        """Delete a customer session."""
        ph = self.backend.get_placeholder()
        self.backend.execute(
            f'DELETE FROM customer_sessions WHERE id = {ph}',
            (session_id,)
        )
        self.backend.commit()
        return True

    def delete_customer_sessions_by_user(self, user_id: str) -> bool:
        """Delete all sessions for a user (logout everywhere)."""
        ph = self.backend.get_placeholder()
        self.backend.execute(
            f'DELETE FROM customer_sessions WHERE user_id = {ph}',
            (user_id,)
        )
        self.backend.commit()
        return True

    def cleanup_expired_sessions(self) -> int:
        """Remove expired customer sessions."""
        ph = self.backend.get_placeholder()
        now = datetime.now(UTC).isoformat()
        self.backend.execute(
            f'DELETE FROM customer_sessions WHERE expires_at < {ph}',
            (now,)
        )
        self.backend.commit()
        return self.backend.cursor.rowcount if hasattr(self.backend, 'cursor') else 0

    # ========================================================================
    # Customer Portal Data Access
    # ========================================================================

    def get_customer_user_with_details(self, user_id: str) -> Optional[Dict[str, Any]]:
        """Get customer user with license and customer details."""
        ph = self.backend.get_placeholder()
        row = self.backend.fetchone(
            f'''SELECT u.*,
                       l.name as license_name, l.email as license_email,
                       l.organization, l.license_type, l.max_seats,
                       l.expiry_date, l.status as license_status,
                       l.issue_date, l.hardware_bound,
                       c.name as customer_name, c.company as customer_company,
                       c.phone, c.address_line1, c.address_line2,
                       c.city, c.state, c.postal_code, c.country,
                       p.display_name as plan_name, p.price_monthly as plan_price
                FROM customer_users u
                JOIN licenses l ON u.license_id = l.id
                LEFT JOIN customers c ON u.customer_id = c.id
                LEFT JOIN billing_plans p ON l.plan_id = p.id
                WHERE u.id = {ph}''',
            (user_id,)
        )
        return dict(row) if row else None

    def get_customer_portal_licenses(self, user_id: str) -> List[Dict[str, Any]]:
        """Get all licenses accessible to a customer user (via customer_id or direct license)."""
        ph = self.backend.get_placeholder()
        user = self.get_customer_user(user_id)
        if not user:
            return []

        # Get the user's direct license
        licenses = []
        license_data = self.get_license(user['license_id'])
        if license_data:
            # Add seat count
            license_data['active_seats'] = self.get_active_seat_count(user['license_id'])
            licenses.append(license_data)

        return licenses

    def get_customer_portal_seats(self, license_id: str) -> List[Dict[str, Any]]:
        """Get seats for a license (for customer portal)."""
        return self.get_license_seats(license_id)

    def get_customer_portal_invoices(self, user_id: str, limit: int = 20) -> List[Dict[str, Any]]:
        """Get invoices for a customer user."""
        ph = self.backend.get_placeholder()
        user = self.get_customer_user(user_id)
        if not user:
            return []

        # Get invoices linked to user's license or customer_id
        rows = self.backend.fetchall(
            f'''SELECT i.*
                FROM invoices i
                WHERE i.license_id = {ph} OR i.customer_id = {ph}
                ORDER BY i.created_at DESC
                LIMIT {ph}''',
            (user['license_id'], user.get('customer_id'), limit)
        )
        return [dict(row) for row in rows]

    def get_customer_portal_subscription(self, license_id: str) -> Optional[Dict[str, Any]]:
        """Get subscription for a license (for customer portal)."""
        return self.get_subscription(license_id)

    # ========================================================================
    # Invoice Scheduler Methods (Overdue Detection)
    # ========================================================================

    def get_invoices_past_due(self) -> List[Dict[str, Any]]:
        """Get invoices that are past due date but not yet marked as overdue."""
        ph = self.backend.get_placeholder()
        now = datetime.now(UTC).isoformat()
        rows = self.backend.fetchall(
            f'''SELECT i.*, c.name as customer_name, c.email as customer_email
                FROM invoices i
                LEFT JOIN customers c ON i.customer_id = c.id
                WHERE i.status = 'pending'
                  AND i.due_date IS NOT NULL
                  AND i.due_date < {ph}
                ORDER BY i.due_date ASC''',
            (now,)
        )
        return [dict(row) for row in rows]

    def get_invoices_due_in_days(self, days: int) -> List[Dict[str, Any]]:
        """Get invoices due within the specified number of days."""
        ph = self.backend.get_placeholder()
        now = datetime.now(UTC)
        target_date = (now + timedelta(days=days)).date().isoformat()
        today = now.date().isoformat()
        rows = self.backend.fetchall(
            f'''SELECT i.*, c.name as customer_name, c.email as customer_email
                FROM invoices i
                LEFT JOIN customers c ON i.customer_id = c.id
                WHERE i.status = 'pending'
                  AND i.due_date IS NOT NULL
                  AND DATE(i.due_date) = {ph}
                ORDER BY i.due_date ASC''',
            (target_date,)
        )
        return [dict(row) for row in rows]

    def get_invoices_severely_overdue(self, days: int = 7) -> List[Dict[str, Any]]:
        """Get invoices that are overdue by more than the specified days."""
        ph = self.backend.get_placeholder()
        cutoff = (datetime.now(UTC) - timedelta(days=days)).isoformat()
        rows = self.backend.fetchall(
            f'''SELECT i.*, c.name as customer_name, c.email as customer_email
                FROM invoices i
                LEFT JOIN customers c ON i.customer_id = c.id
                WHERE i.status IN ('pending', 'overdue')
                  AND i.due_date IS NOT NULL
                  AND i.due_date < {ph}
                ORDER BY i.due_date ASC''',
            (cutoff,)
        )
        return [dict(row) for row in rows]

    def log_invoice_reminder(self, invoice_id: str, days_before: int) -> bool:
        """Log that an invoice reminder was sent."""
        ph = self.backend.get_placeholder()
        now = self._now()
        try:
            self.backend.execute(f'''
                INSERT INTO invoice_reminders (invoice_id, days_before, sent_at)
                VALUES ({ph}, {ph}, {ph})
            ''', (invoice_id, days_before, now))
            self.backend.commit()
            return True
        except Exception as e:
            # Table might not exist yet, log and continue
            logger = __import__('logging').getLogger(__name__)
            logger.warning(f"Could not log invoice reminder: {str(e)}")
            return False

    def get_last_invoice_reminder(self, invoice_id: str) -> Optional[Dict[str, Any]]:
        """Get the last reminder sent for an invoice."""
        ph = self.backend.get_placeholder()
        try:
            row = self.backend.fetchone(
                f'''SELECT * FROM invoice_reminders
                    WHERE invoice_id = {ph}
                    ORDER BY sent_at DESC
                    LIMIT 1''',
                (invoice_id,)
            )
            return dict(row) if row else None
        except Exception:
            return None

    # ========================================================================
    # License Scheduler Methods (Expiry Detection)
    # ========================================================================

    def get_licenses_expiring_in_days(self, days: int) -> List[Dict[str, Any]]:
        """Get licenses expiring within the specified number of days."""
        ph = self.backend.get_placeholder()
        now = datetime.now(UTC)
        target_date = (now + timedelta(days=days)).date().isoformat()
        rows = self.backend.fetchall(
            f'''SELECT l.*, c.name as customer_name, c.email as customer_email
                FROM licenses l
                LEFT JOIN customers c ON l.customer_id = c.id
                WHERE l.status = 'active'
                  AND l.expiry_date IS NOT NULL
                  AND DATE(l.expiry_date) = {ph}
                ORDER BY l.expiry_date ASC''',
            (target_date,)
        )
        return [dict(row) for row in rows]

    def get_licenses_expired_today(self) -> List[Dict[str, Any]]:
        """Get licenses that expired today (for marking as expired)."""
        ph = self.backend.get_placeholder()
        now = datetime.now(UTC)
        today_start = now.replace(hour=0, minute=0, second=0, microsecond=0).isoformat()
        today_end = now.isoformat()
        rows = self.backend.fetchall(
            f'''SELECT l.*, c.name as customer_name, c.email as customer_email
                FROM licenses l
                LEFT JOIN customers c ON l.customer_id = c.id
                WHERE l.status = 'active'
                  AND l.expiry_date IS NOT NULL
                  AND l.expiry_date <= {ph}
                ORDER BY l.expiry_date ASC''',
            (today_end,)
        )
        return [dict(row) for row in rows]

    def suspend_license(self, license_id: str, reason: str = '') -> bool:
        """Suspend a license."""
        return self.update_license(license_id, {
            'status': 'suspended',
            'notes': f'Suspended: {reason}' if reason else 'Suspended'
        })

    def reactivate_license(self, license_id: str, reason: str = '') -> bool:
        """Reactivate a suspended license."""
        return self.update_license(license_id, {
            'status': 'active',
            'notes': f'Reactivated: {reason}' if reason else 'Reactivated'
        })

    # ========================================================================
    # Statistics
    # ========================================================================

    def get_statistics(self) -> Dict[str, Any]:
        """Get comprehensive statistics for dashboard."""
        stats = {}
        ph = self.backend.get_placeholder()

        # License counts by status
        for status in ['active', 'expired', 'revoked', 'deleted']:
            row = self.backend.fetchone(
                f"SELECT COUNT(*) as count FROM licenses WHERE status = {ph}",
                (status,)
            )
            stats[f'{status}_licenses'] = row['count'] if row else 0

        # Total licenses
        row = self.backend.fetchone('SELECT COUNT(*) as count FROM licenses')
        stats['total_licenses'] = row['count'] if row else 0

        # Count by type
        stats['by_type'] = {}
        for type_id, type_name in LICENSE_TYPES.items():
            row = self.backend.fetchone(
                f'SELECT COUNT(*) as count FROM licenses WHERE license_type = {ph}',
                (type_id,)
            )
            stats['by_type'][type_name] = row['count'] if row else 0

        # Customer counts
        row = self.backend.fetchone('SELECT COUNT(*) as count FROM customers')
        stats['total_customers'] = row['count'] if row else 0

        row = self.backend.fetchone(
            "SELECT COUNT(*) as count FROM customers WHERE status = 'active'"
        )
        stats['active_customers'] = row['count'] if row else 0

        # Active subscriptions
        row = self.backend.fetchone(
            "SELECT COUNT(*) as count FROM subscriptions WHERE status = 'active'"
        )
        stats['active_subscriptions'] = row['count'] if row else 0

        # Recent activations (last 24 hours)
        cutoff = (datetime.now(UTC) - timedelta(hours=24)).isoformat()
        row = self.backend.fetchone(
            f'SELECT COUNT(*) as count FROM activation_logs WHERE created_at > {ph}',
            (cutoff,)
        )
        stats['recent_activations'] = row['count'] if row else 0

        # Total active seats
        row = self.backend.fetchone(
            f"SELECT COUNT(*) as count FROM license_seats WHERE is_active = {ph}",
            (self._bool_val(True),)
        )
        stats['active_seats'] = row['count'] if row else 0

        # Total unique hardware fingerprints
        row = self.backend.fetchone(
            f"SELECT COUNT(DISTINCT hardware_fingerprint) as count FROM license_seats WHERE is_active = {ph}",
            (self._bool_val(True),)
        )
        stats['unique_devices'] = row['count'] if row else 0

        # Revenue stats (last 30 days)
        cutoff_30 = (datetime.now(UTC) - timedelta(days=30)).isoformat()
        row = self.backend.fetchone(
            f'''SELECT COALESCE(SUM(amount), 0) as total
                FROM billing_history
                WHERE type = 'payment' AND status = 'succeeded' AND created_at > {ph}''',
            (cutoff_30,)
        )
        stats['revenue_30_days'] = float(row['total']) if row else 0

        # Invoice counts
        row = self.backend.fetchone(
            "SELECT COUNT(*) as count FROM invoices WHERE status = 'paid'"
        )
        stats['paid_invoices'] = row['count'] if row else 0

        row = self.backend.fetchone(
            "SELECT COUNT(*) as count FROM invoices WHERE status = 'pending'"
        )
        stats['pending_invoices'] = row['count'] if row else 0

        return stats


# ============================================================================
# Global Database Instance
# ============================================================================

_db_instance = None


def get_database() -> LicenseDatabase:
    """Get the global database instance."""
    global _db_instance
    if _db_instance is None:
        _db_instance = LicenseDatabase()
    return _db_instance
