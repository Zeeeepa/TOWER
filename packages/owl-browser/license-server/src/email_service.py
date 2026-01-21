"""
Owl Browser License Server - Email Service

Handles sending emails via SMTP with template support.
"""

import smtplib
import ssl
import re
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from typing import Dict, Any, Optional, Tuple
from datetime import datetime
import logging

logger = logging.getLogger(__name__)


class EmailService:
    """
    Email service for sending templated emails via SMTP.
    """

    def __init__(self, db):
        """
        Initialize email service with database connection.

        Args:
            db: LicenseDatabase instance for settings and logging
        """
        self.db = db
        self._smtp_config = None

    def _get_smtp_config(self) -> Dict[str, Any]:
        """Get SMTP configuration from database settings."""
        if self._smtp_config is None:
            self._smtp_config = {
                'host': self.db.get_setting('smtp_host', ''),
                'port': int(self.db.get_setting('smtp_port', '587')),
                'username': self.db.get_setting('smtp_username', ''),
                'password': self.db.get_setting('smtp_password', ''),
                'use_tls': self.db.get_setting('smtp_tls', 'true').lower() == 'true',
                'from_email': self.db.get_setting('smtp_from_email', ''),
                'from_name': self.db.get_setting('smtp_from_name', 'Owl Browser Licensing'),
            }
        return self._smtp_config

    def reload_config(self):
        """Force reload of SMTP configuration."""
        self._smtp_config = None

    def is_configured(self) -> bool:
        """Check if SMTP is properly configured."""
        config = self._get_smtp_config()
        return bool(config['host'] and config['username'] and config['from_email'])

    def _substitute_variables(self, template: str, variables: Dict[str, Any]) -> str:
        """
        Substitute {{variable}} placeholders in template.

        Args:
            template: Template string with {{variable}} placeholders
            variables: Dictionary of variable names to values

        Returns:
            Template with variables substituted
        """
        def replace_var(match):
            var_name = match.group(1).strip()
            return str(variables.get(var_name, match.group(0)))

        return re.sub(r'\{\{(\s*\w+\s*)\}\}', replace_var, template)

    def send_email(
        self,
        to_email: str,
        subject: str,
        body_html: str,
        body_text: Optional[str] = None,
        customer_id: Optional[str] = None,
        template_id: Optional[str] = None
    ) -> Tuple[bool, str]:
        """
        Send an email.

        Args:
            to_email: Recipient email address
            subject: Email subject
            body_html: HTML body content
            body_text: Plain text body (optional, will strip HTML if not provided)
            customer_id: Customer ID for logging (optional)
            template_id: Template ID for logging (optional)

        Returns:
            Tuple of (success, message/error)
        """
        config = self._get_smtp_config()

        if not self.is_configured():
            error_msg = "SMTP not configured"
            logger.warning(error_msg)
            self._log_email(customer_id, template_id, to_email, subject, 'failed', error_msg)
            return False, error_msg

        # Create plain text version if not provided
        if body_text is None:
            body_text = re.sub(r'<[^>]+>', '', body_html)

        # Create message
        msg = MIMEMultipart('alternative')
        msg['Subject'] = subject
        msg['From'] = f"{config['from_name']} <{config['from_email']}>"
        msg['To'] = to_email

        # Attach both plain text and HTML versions
        part1 = MIMEText(body_text, 'plain')
        part2 = MIMEText(body_html, 'html')
        msg.attach(part1)
        msg.attach(part2)

        try:
            # Connect to SMTP server
            if config['use_tls']:
                context = ssl.create_default_context()
                server = smtplib.SMTP(config['host'], config['port'])
                server.starttls(context=context)
            else:
                server = smtplib.SMTP(config['host'], config['port'])

            # Login and send
            if config['username'] and config['password']:
                server.login(config['username'], config['password'])

            server.sendmail(config['from_email'], to_email, msg.as_string())
            server.quit()

            logger.info(f"Email sent successfully to {to_email}: {subject}")
            self._log_email(customer_id, template_id, to_email, subject, 'sent', None)
            return True, "Email sent successfully"

        except smtplib.SMTPAuthenticationError as e:
            error_msg = f"SMTP authentication failed: {str(e)}"
            logger.error(error_msg)
            self._log_email(customer_id, template_id, to_email, subject, 'failed', error_msg)
            return False, error_msg

        except smtplib.SMTPException as e:
            error_msg = f"SMTP error: {str(e)}"
            logger.error(error_msg)
            self._log_email(customer_id, template_id, to_email, subject, 'failed', error_msg)
            return False, error_msg

        except Exception as e:
            error_msg = f"Failed to send email: {str(e)}"
            logger.error(error_msg)
            self._log_email(customer_id, template_id, to_email, subject, 'failed', error_msg)
            return False, error_msg

    def _log_email(
        self,
        customer_id: Optional[str],
        template_id: Optional[str],
        to_email: str,
        subject: str,
        status: str,
        error_message: Optional[str]
    ):
        """Log email send attempt to database."""
        try:
            self.db.log_email({
                'customer_id': customer_id,
                'template_id': template_id,
                'to_email': to_email,
                'subject': subject,
                'status': status,
                'provider': 'smtp',
                'error_message': error_message,
                'sent_at': datetime.utcnow().isoformat() if status == 'sent' else None
            })
        except Exception as e:
            logger.error(f"Failed to log email: {str(e)}")

    def send_template_email(
        self,
        template_name: str,
        to_email: str,
        variables: Dict[str, Any],
        customer_id: Optional[str] = None
    ) -> Tuple[bool, str]:
        """
        Send an email using a template from the database.

        Args:
            template_name: Name of the template (e.g., 'welcome', 'invoice_created')
            to_email: Recipient email address
            variables: Dictionary of variables to substitute in template
            customer_id: Customer ID for logging (optional)

        Returns:
            Tuple of (success, message/error)
        """
        # Get template
        template = self.db.get_email_template(template_name)
        if not template:
            error_msg = f"Email template '{template_name}' not found"
            logger.error(error_msg)
            return False, error_msg

        if not template.get('is_active'):
            error_msg = f"Email template '{template_name}' is disabled"
            logger.warning(error_msg)
            return False, error_msg

        # Substitute variables
        subject = self._substitute_variables(template['subject'], variables)
        body_html = self._substitute_variables(template['body_html'], variables)
        body_text = self._substitute_variables(template.get('body_text', ''), variables) if template.get('body_text') else None

        return self.send_email(
            to_email=to_email,
            subject=subject,
            body_html=body_html,
            body_text=body_text,
            customer_id=customer_id,
            template_id=template.get('id')
        )

    # =========================================================================
    # Convenience methods for common emails
    # =========================================================================

    def send_invoice_created(self, invoice: Dict[str, Any], customer: Dict[str, Any]) -> Tuple[bool, str]:
        """Send invoice created notification."""
        variables = {
            'customer_name': customer.get('name', ''),
            'invoice_number': invoice.get('invoice_number', ''),
            'invoice_total': f"${invoice.get('total', 0):.2f}",
            'invoice_currency': invoice.get('currency', 'USD'),
            'due_date': invoice.get('due_date', 'N/A'),
            'company_name': customer.get('company', ''),
        }
        return self.send_template_email(
            'invoice_created',
            customer.get('email', ''),
            variables,
            customer.get('id')
        )

    def send_invoice_reminder(self, invoice: Dict[str, Any], customer: Dict[str, Any], days_overdue: int = 0) -> Tuple[bool, str]:
        """Send invoice payment reminder."""
        variables = {
            'customer_name': customer.get('name', ''),
            'invoice_number': invoice.get('invoice_number', ''),
            'invoice_total': f"${invoice.get('total', 0):.2f}",
            'amount_due': f"${invoice.get('amount_due', invoice.get('total', 0)):.2f}",
            'due_date': invoice.get('due_date', 'N/A'),
            'days_overdue': str(days_overdue),
        }
        return self.send_template_email(
            'invoice_reminder',
            customer.get('email', ''),
            variables,
            customer.get('id')
        )

    def send_invoice_overdue(self, invoice: Dict[str, Any], customer: Dict[str, Any], days_overdue: int) -> Tuple[bool, str]:
        """Send invoice overdue notification."""
        variables = {
            'customer_name': customer.get('name', ''),
            'invoice_number': invoice.get('invoice_number', ''),
            'invoice_total': f"${invoice.get('total', 0):.2f}",
            'amount_due': f"${invoice.get('amount_due', invoice.get('total', 0)):.2f}",
            'due_date': invoice.get('due_date', 'N/A'),
            'days_overdue': str(days_overdue),
        }
        return self.send_template_email(
            'invoice_overdue',
            customer.get('email', ''),
            variables,
            customer.get('id')
        )

    def send_payment_received(self, invoice: Dict[str, Any], customer: Dict[str, Any]) -> Tuple[bool, str]:
        """Send payment received confirmation."""
        variables = {
            'customer_name': customer.get('name', ''),
            'invoice_number': invoice.get('invoice_number', ''),
            'amount_paid': f"${invoice.get('total', 0):.2f}",
            'payment_date': datetime.utcnow().strftime('%Y-%m-%d'),
        }
        return self.send_template_email(
            'payment_received',
            customer.get('email', ''),
            variables,
            customer.get('id')
        )

    def send_license_expiring(self, license: Dict[str, Any], customer: Dict[str, Any], days_remaining: int) -> Tuple[bool, str]:
        """Send license expiring warning."""
        variables = {
            'customer_name': customer.get('name', ''),
            'license_name': license.get('name', ''),
            'license_id': license.get('id', ''),
            'expiry_date': license.get('expiry_date', 'N/A'),
            'days_remaining': str(days_remaining),
        }
        return self.send_template_email(
            'subscription_expiring',
            customer.get('email', ''),
            variables,
            customer.get('id')
        )

    def send_license_expired(self, license: Dict[str, Any], customer: Dict[str, Any]) -> Tuple[bool, str]:
        """Send license expired notification."""
        variables = {
            'customer_name': customer.get('name', ''),
            'license_name': license.get('name', ''),
            'license_id': license.get('id', ''),
            'expiry_date': license.get('expiry_date', 'N/A'),
        }
        return self.send_template_email(
            'license_expired',
            customer.get('email', ''),
            variables,
            customer.get('id')
        )
