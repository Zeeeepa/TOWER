"""
Owl Browser License Server - Scheduler Service

Background job scheduler using APScheduler for:
- Invoice due date monitoring
- Overdue invoice handling and license suspension
- Email notifications (invoice created, reminders, overdue)
- License expiry monitoring
- Cleanup tasks (nonces, sessions)
"""

import logging
from datetime import datetime, timedelta, UTC
from typing import Optional
from apscheduler.schedulers.background import BackgroundScheduler
from apscheduler.triggers.cron import CronTrigger
from apscheduler.triggers.interval import IntervalTrigger

logger = logging.getLogger(__name__)


class SchedulerService:
    """
    Background job scheduler for automated license and invoice management.
    """

    def __init__(self, db, email_service=None):
        """
        Initialize scheduler service.

        Args:
            db: LicenseDatabase instance
            email_service: EmailService instance (optional)
        """
        self.db = db
        self.email_service = email_service
        self.scheduler = BackgroundScheduler(
            timezone='UTC',
            job_defaults={
                'coalesce': True,  # Combine multiple missed executions into one
                'max_instances': 1,  # Prevent overlapping job executions
                'misfire_grace_time': 60 * 15  # 15 minutes grace period
            }
        )
        self._setup_jobs()

    def _setup_jobs(self):
        """Set up scheduled jobs."""
        # Check for overdue invoices - run every hour
        self.scheduler.add_job(
            self.check_overdue_invoices,
            IntervalTrigger(hours=1),
            id='check_overdue_invoices',
            name='Check Overdue Invoices',
            replace_existing=True
        )

        # Send invoice reminders - run daily at 9 AM UTC
        self.scheduler.add_job(
            self.send_invoice_reminders,
            CronTrigger(hour=9, minute=0),
            id='send_invoice_reminders',
            name='Send Invoice Reminders',
            replace_existing=True
        )

        # Process severely overdue invoices and suspend licenses - run every 6 hours
        self.scheduler.add_job(
            self.process_severely_overdue,
            IntervalTrigger(hours=6),
            id='process_severely_overdue',
            name='Process Severely Overdue Invoices',
            replace_existing=True
        )

        # Check expiring licenses - run daily at 8 AM UTC
        self.scheduler.add_job(
            self.check_expiring_licenses,
            CronTrigger(hour=8, minute=0),
            id='check_expiring_licenses',
            name='Check Expiring Licenses',
            replace_existing=True
        )

        # Clean up old nonces - run daily at 3 AM UTC
        self.scheduler.add_job(
            self.cleanup_old_nonces,
            CronTrigger(hour=3, minute=0),
            id='cleanup_old_nonces',
            name='Cleanup Old Nonces',
            replace_existing=True
        )

        # Clean up expired sessions - run every 4 hours
        self.scheduler.add_job(
            self.cleanup_expired_sessions,
            IntervalTrigger(hours=4),
            id='cleanup_expired_sessions',
            name='Cleanup Expired Sessions',
            replace_existing=True
        )

        logger.info("Scheduler jobs configured successfully")

    def start(self):
        """Start the scheduler."""
        if not self.scheduler.running:
            self.scheduler.start()
            logger.info("Scheduler started")

    def stop(self, wait=True):
        """Stop the scheduler."""
        if self.scheduler.running:
            self.scheduler.shutdown(wait=wait)
            logger.info("Scheduler stopped")

    def run_job_now(self, job_id: str):
        """Manually trigger a job to run immediately."""
        job = self.scheduler.get_job(job_id)
        if job:
            job.modify(next_run_time=datetime.now(UTC))
            logger.info(f"Job {job_id} triggered to run immediately")
        else:
            logger.warning(f"Job {job_id} not found")

    def get_jobs_status(self) -> list:
        """Get status of all scheduled jobs."""
        jobs = []
        for job in self.scheduler.get_jobs():
            jobs.append({
                'id': job.id,
                'name': job.name,
                'next_run': job.next_run_time.isoformat() if job.next_run_time else None,
                'trigger': str(job.trigger)
            })
        return jobs

    # =========================================================================
    # Job: Check Overdue Invoices
    # =========================================================================

    def check_overdue_invoices(self):
        """
        Check for invoices that have passed their due date and mark them as overdue.
        Also sends overdue notification emails.
        """
        logger.info("Running job: check_overdue_invoices")
        try:
            overdue_invoices = self.db.get_invoices_past_due()
            count = 0

            for invoice in overdue_invoices:
                # Mark as overdue
                self.db.update_invoice(invoice['id'], {'status': 'overdue'})
                count += 1

                # Send overdue email notification
                if self.email_service:
                    customer = self.db.get_customer(invoice['customer_id'])
                    if customer:
                        days_overdue = self._calculate_days_overdue(invoice['due_date'])
                        self.email_service.send_invoice_overdue(invoice, customer, days_overdue)

            logger.info(f"Marked {count} invoices as overdue")
            return count

        except Exception as e:
            logger.error(f"Error checking overdue invoices: {str(e)}")
            return 0

    # =========================================================================
    # Job: Send Invoice Reminders
    # =========================================================================

    def send_invoice_reminders(self):
        """
        Send reminder emails for invoices due within the next 3 days.
        """
        logger.info("Running job: send_invoice_reminders")
        if not self.email_service:
            logger.warning("Email service not configured, skipping reminders")
            return 0

        try:
            # Get invoices due in 1, 3, and 7 days
            reminder_days = [1, 3, 7]
            count = 0

            for days in reminder_days:
                invoices = self.db.get_invoices_due_in_days(days)
                for invoice in invoices:
                    # Check if we already sent a reminder today
                    if self._should_send_reminder(invoice, days):
                        customer = self.db.get_customer(invoice['customer_id'])
                        if customer:
                            success, _ = self.email_service.send_invoice_reminder(
                                invoice, customer, days_overdue=0
                            )
                            if success:
                                count += 1
                                # Log that reminder was sent
                                self.db.log_invoice_reminder(invoice['id'], days)

            logger.info(f"Sent {count} invoice reminders")
            return count

        except Exception as e:
            logger.error(f"Error sending invoice reminders: {str(e)}")
            return 0

    def _should_send_reminder(self, invoice: dict, days_before: int) -> bool:
        """Check if we should send a reminder (not already sent today)."""
        # Get last reminder date for this invoice
        last_reminder = self.db.get_last_invoice_reminder(invoice['id'])
        if not last_reminder:
            return True

        # Don't send more than one reminder per day
        today = datetime.now(UTC).date()
        last_date = datetime.fromisoformat(last_reminder['sent_at'].replace('Z', '+00:00')).date()
        return today > last_date

    # =========================================================================
    # Job: Process Severely Overdue Invoices
    # =========================================================================

    def process_severely_overdue(self):
        """
        Process invoices that are severely overdue (>7 days) and suspend associated licenses.
        Also sends license suspension notification.
        """
        logger.info("Running job: process_severely_overdue")
        grace_period_days = int(self.db.get_setting('grace_period_days') or 7)

        try:
            severely_overdue = self.db.get_invoices_severely_overdue(grace_period_days)
            count = 0

            for invoice in severely_overdue:
                # Check if license is already suspended
                license_id = invoice.get('license_id')
                if not license_id:
                    continue

                license_data = self.db.get_license(license_id)
                if not license_data or license_data['status'] == 'suspended':
                    continue

                # Suspend the license
                self.db.update_license(license_id, {
                    'status': 'suspended',
                    'notes': f"Suspended due to unpaid invoice {invoice['invoice_number']}"
                })
                count += 1

                # Send suspension notification
                if self.email_service:
                    customer = self.db.get_customer(invoice['customer_id'])
                    if customer:
                        self.email_service.send_template_email(
                            'license_suspended',
                            customer['email'],
                            {
                                'customer_name': customer.get('name', ''),
                                'license_name': license_data.get('name', ''),
                                'license_id': license_id,
                                'invoice_number': invoice['invoice_number'],
                                'amount_due': f"${invoice.get('amount_due', invoice.get('total', 0)):.2f}",
                            },
                            customer['id']
                        )

                logger.info(f"Suspended license {license_id} for overdue invoice {invoice['invoice_number']}")

            logger.info(f"Suspended {count} licenses due to severely overdue invoices")
            return count

        except Exception as e:
            logger.error(f"Error processing severely overdue invoices: {str(e)}")
            return 0

    # =========================================================================
    # Job: Check Expiring Licenses
    # =========================================================================

    def check_expiring_licenses(self):
        """
        Check for licenses expiring within the next 7 and 30 days.
        Sends expiry warning emails.
        """
        logger.info("Running job: check_expiring_licenses")
        if not self.email_service:
            logger.warning("Email service not configured, skipping expiry checks")
            return 0

        try:
            count = 0

            # Check licenses expiring in 7 days
            expiring_7 = self.db.get_licenses_expiring_in_days(7)
            for license_data in expiring_7:
                if self._should_send_expiry_warning(license_data, 7):
                    customer = self.db.get_customer(license_data.get('customer_id'))
                    if customer:
                        success, _ = self.email_service.send_license_expiring(
                            license_data, customer, days_remaining=7
                        )
                        if success:
                            count += 1

            # Check licenses expiring in 30 days
            expiring_30 = self.db.get_licenses_expiring_in_days(30)
            for license_data in expiring_30:
                if self._should_send_expiry_warning(license_data, 30):
                    customer = self.db.get_customer(license_data.get('customer_id'))
                    if customer:
                        success, _ = self.email_service.send_license_expiring(
                            license_data, customer, days_remaining=30
                        )
                        if success:
                            count += 1

            # Check for expired licenses and mark them
            expired = self.db.get_licenses_expired_today()
            for license_data in expired:
                if license_data['status'] == 'active':
                    self.db.update_license(license_data['id'], {'status': 'expired'})
                    customer = self.db.get_customer(license_data.get('customer_id'))
                    if customer and self.email_service:
                        self.email_service.send_license_expired(license_data, customer)

            logger.info(f"Sent {count} license expiry warnings")
            return count

        except Exception as e:
            logger.error(f"Error checking expiring licenses: {str(e)}")
            return 0

    def _should_send_expiry_warning(self, license_data: dict, days_before: int) -> bool:
        """Check if we should send an expiry warning (not already sent for this interval)."""
        # This could be enhanced to check email_log for previous sends
        # For now, we'll let it send daily during the warning window
        return True

    # =========================================================================
    # Job: Cleanup Old Nonces
    # =========================================================================

    def cleanup_old_nonces(self):
        """Clean up nonces older than 30 days."""
        logger.info("Running job: cleanup_old_nonces")
        try:
            self.db.cleanup_old_nonces(days=30)
            logger.info("Old nonces cleaned up successfully")
        except Exception as e:
            logger.error(f"Error cleaning up old nonces: {str(e)}")

    # =========================================================================
    # Job: Cleanup Expired Sessions
    # =========================================================================

    def cleanup_expired_sessions(self):
        """Clean up expired customer sessions."""
        logger.info("Running job: cleanup_expired_sessions")
        try:
            count = self.db.cleanup_expired_sessions()
            logger.info(f"Cleaned up {count} expired sessions")
            return count
        except Exception as e:
            logger.error(f"Error cleaning up expired sessions: {str(e)}")
            return 0

    # =========================================================================
    # Helper Methods
    # =========================================================================

    def _calculate_days_overdue(self, due_date) -> int:
        """Calculate how many days an invoice is overdue."""
        if not due_date:
            return 0

        if isinstance(due_date, str):
            due = datetime.fromisoformat(due_date.replace('Z', '+00:00'))
        else:
            due = due_date

        if due.tzinfo is None:
            due = due.replace(tzinfo=UTC)

        now = datetime.now(UTC)
        delta = now - due
        return max(0, delta.days)

    # =========================================================================
    # Manual Actions (for admin use)
    # =========================================================================

    def send_invoice_created_email(self, invoice_id: str) -> tuple:
        """
        Manually send invoice created email.
        Called when a new invoice is created.
        """
        if not self.email_service:
            return False, "Email service not configured"

        try:
            invoice = self.db.get_invoice(invoice_id)
            if not invoice:
                return False, "Invoice not found"

            customer = self.db.get_customer(invoice['customer_id'])
            if not customer:
                return False, "Customer not found"

            return self.email_service.send_invoice_created(invoice, customer)

        except Exception as e:
            logger.error(f"Error sending invoice created email: {str(e)}")
            return False, str(e)

    def reactivate_license_on_payment(self, invoice_id: str) -> tuple:
        """
        Reactivate a suspended license when invoice is paid.
        Called when an invoice is marked as paid.
        """
        try:
            invoice = self.db.get_invoice(invoice_id)
            if not invoice:
                return False, "Invoice not found"

            license_id = invoice.get('license_id')
            if not license_id:
                return True, "No license associated with invoice"

            license_data = self.db.get_license(license_id)
            if not license_data:
                return False, "License not found"

            # Only reactivate if suspended
            if license_data['status'] == 'suspended':
                self.db.update_license(license_id, {
                    'status': 'active',
                    'notes': f"Reactivated after payment of invoice {invoice['invoice_number']}"
                })
                logger.info(f"Reactivated license {license_id} after payment")

                # Send payment confirmation
                if self.email_service:
                    customer = self.db.get_customer(invoice['customer_id'])
                    if customer:
                        self.email_service.send_payment_received(invoice, customer)

            return True, "License reactivated"

        except Exception as e:
            logger.error(f"Error reactivating license: {str(e)}")
            return False, str(e)


# ============================================================================
# Global Scheduler Instance
# ============================================================================

_scheduler_instance: Optional[SchedulerService] = None


def get_scheduler(db=None, email_service=None) -> Optional[SchedulerService]:
    """Get the global scheduler instance."""
    global _scheduler_instance
    if _scheduler_instance is None and db is not None:
        _scheduler_instance = SchedulerService(db, email_service)
    return _scheduler_instance


def start_scheduler(db, email_service=None) -> SchedulerService:
    """Start the global scheduler."""
    global _scheduler_instance
    if _scheduler_instance is None:
        _scheduler_instance = SchedulerService(db, email_service)
    _scheduler_instance.start()
    return _scheduler_instance


def stop_scheduler():
    """Stop the global scheduler."""
    global _scheduler_instance
    if _scheduler_instance:
        _scheduler_instance.stop()
        _scheduler_instance = None
