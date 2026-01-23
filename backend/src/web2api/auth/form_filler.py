"""
Automated form filling with CAPTCHA handling.

Integrates Owl-Browser commands for login automation.
"""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING, Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class FormFiller:
    """
    Automated form filler for authentication flows.

    Features:
    - Automatic login form detection and filling
    - CAPTCHA detection and solving (using Owl-Browser commands)
    - Multi-step login flows
    - 2FA handling (with manual intervention if needed)
    - Login verification
    """

    def __init__(self) -> None:
        """Initialize form filler."""
        self._log = logger.bind(component="form_filler")

    async def detect_login_form(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any] | None:
        """
        Detect login form elements on the page.

        Uses browser_find_element to locate email/username and password fields.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            Dictionary with form selectors, or None if not found
        """
        try:
            # Find email/username field
            email_field = await browser.browser_find_element({
                "context_id": context_id,
                "description": "email input or username input field",
            })

            # Find password field
            password_field = await browser.browser_find_element({
                "context_id": context_id,
                "description": "password input field",
            })

            # Find submit button
            submit_button = await browser.browser_find_element({
                "context_id": context_id,
                "description": "submit button, login button, or sign in button",
            })

            if not email_field.get("found") or not password_field.get("found"):
                self._log.info("No login form detected")
                return None

            form_info = {
                "email_selector": email_field.get("selector"),
                "password_selector": password_field.get("selector"),
                "submit_selector": submit_button.get("selector") if submit_button.get("found") else None,
            }

            self._log.info(
                "Login form detected",
                form_info=form_info,
            )

            return form_info

        except Exception as e:
            self._log.error(
                "Login form detection failed",
                error=str(e),
            )
            return None

    async def handle_captcha(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any] | None:
        """
        Detect and solve CAPTCHA if present.

        Uses Owl-Browser CAPTCHA commands:
        - browser_detect_captcha: Detect CAPTCHA presence
        - browser_classify_captcha: Classify CAPTCHA type
        - browser_solve_text_captcha: Solve text-based CAPTCHA
        - browser_solve_image_captcha: Solve image-based CAPTCHA
        - browser_solve_captcha: Universal CAPTCHA solver

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            CAPTCHA solving result, or None if no CAPTCHA found
        """
        try:
            # Step 1: Detect CAPTCHA
            detection_result = await browser.browser_detect_captcha({
                "context_id": context_id,
            })

            if not detection_result.get("found"):
                self._log.debug("No CAPTCHA detected")
                return None

            self._log.info("CAPTCHA detected")

            # Step 2: Classify CAPTCHA type
            classification = await browser.browser_classify_captcha({
                "context_id": context_id,
            })

            captcha_type = classification.get("type", "unknown")
            self._log.info("CAPTCHA classified", captcha_type=captcha_type)

            # Step 3: Solve based on type
            solve_result = None

            if captcha_type == "text":
                solve_result = await browser.browser_solve_text_captcha({
                    "context_id": context_id,
                })
            elif captcha_type == "image":
                solve_result = await browser.browser_solve_image_captcha({
                    "context_id": context_id,
                })
            else:
                # Use universal solver
                solve_result = await browser.browser_solve_captcha({
                    "context_id": context_id,
                })

            if solve_result and solve_result.get("success"):
                self._log.info("CAPTCHA solved successfully")
                return solve_result
            else:
                self._log.warning("CAPTCHA solving failed")
                return None

        except Exception as e:
            self._log.error(
                "CAPTCHA handling failed",
                error=str(e),
            )
            return None

    async def fill_and_submit(
        self,
        browser: Browser,
        context_id: str,
        credentials: dict[str, str],
        form_info: dict[str, Any],
    ) -> bool:
        """
        Fill login form and submit.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            credentials: Dictionary with email/username and password
            form_info: Form selectors from detect_login_form

        Returns:
            True if form was filled and submitted successfully
        """
        try:
            email = credentials.get("email") or credentials.get("username")
            password = credentials.get("password")

            if not email or not password:
                self._log.error("Missing credentials")
                return False

            # Fill email field
            await browser.browser_type({
                "context_id": context_id,
                "selector": form_info["email_selector"],
                "text": email,
            })

            self._log.debug("Email field filled")

            # Small delay to appear human-like
            await asyncio.sleep(0.5)

            # Fill password field
            await browser.browser_type({
                "context_id": context_id,
                "selector": form_info["password_selector"],
                "text": password,
            })

            self._log.debug("Password field filled")

            # Small delay before CAPTCHA handling
            await asyncio.sleep(0.5)

            # Handle CAPTCHA if present
            captcha_result = await self.handle_captcha(browser, context_id)
            if captcha_result:
                self._log.info("CAPTCHA solved before submission")

            # Submit form
            if form_info.get("submit_selector"):
                await browser.browser_click({
                    "context_id": context_id,
                    "selector": form_info["submit_selector"],
                })

                self._log.info("Login form submitted")
                return True
            else:
                self._log.warning("No submit button found")
                return False

        except Exception as e:
            self._log.error(
                "Form filling failed",
                error=str(e),
            )
            return False

    async def wait_for_login_success(
        self,
        browser: Browser,
        context_id: str,
        timeout: float = 30.0,
    ) -> bool:
        """
        Wait for successful login after form submission.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            timeout: Maximum time to wait in seconds

        Returns:
            True if login was successful
        """
        try:
            # Wait for navigation
            await browser.browser_wait_for_navigation({
                "context_id": context_id,
                "timeout": int(timeout * 1000),
            })

            # Check if we're still on login page
            current_url_result = await browser.browser_get_current_url({
                "context_id": context_id,
            })

            current_url = current_url_result.get("url", "")

            # If URL contains "login" or "signin", likely failed
            if "login" in current_url.lower() or "signin" in current_url.lower():
                self._log.warning(
                    "Still on login page after submission",
                    current_url=current_url,
                )
                return False

            # Check for error messages
            error_element = await browser.browser_find_element({
                "context_id": context_id,
                "description": "error message or alert showing login failed",
            })

            if error_element.get("found"):
                self._log.warning("Login error detected")
                return False

            self._log.info("Login appears successful")
            return True

        except Exception as e:
            self._log.error(
                "Login verification failed",
                error=str(e),
            )
            return False

    async def complete_login_flow(
        self,
        browser: Browser,
        context_id: str,
        credentials: dict[str, str],
    ) -> bool:
        """
        Complete full login flow.

        Combines detection, filling, CAPTCHA handling, and verification.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            credentials: Login credentials

        Returns:
            True if login was successful
        """
        # Step 1: Detect login form
        form_info = await self.detect_login_form(browser, context_id)
        if not form_info:
            self._log.error("Could not detect login form")
            return False

        # Step 2: Fill and submit form
        submitted = await self.fill_and_submit(
            browser,
            context_id,
            credentials,
            form_info,
        )
        if not submitted:
            self._log.error("Form submission failed")
            return False

        # Step 3: Wait for successful login
        success = await self.wait_for_login_success(browser, context_id)

        if success:
            self._log.info("Login flow completed successfully")
        else:
            self._log.error("Login flow failed")

        return success
