"""
Authentication mechanism detection using Owl-Browser commands.

Detects login forms, OAuth flows, API key authentication, etc.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class AuthDetector:
    """
    Detects authentication mechanisms on web services.

    Uses Owl-Browser commands to analyze auth requirements:
    - browser_find_element: Locate login forms
    - browser_query_page: Query auth components
    - browser_ai_analyze: AI-powered auth flow analysis
    """

    def __init__(self) -> None:
        """Initialize auth detector."""
        self._log = logger.bind(component="auth_detector")

    async def detect_auth_mechanism(
        self,
        browser: Browser,
        context_id: str,
        url: str,
    ) -> dict[str, Any]:
        """
        Detect how the service authenticates users.

        Args:
            browser: Owl-Browser instance
            context_id: Browser context ID
            url: Service URL

        Returns:
            Dictionary with auth configuration
        """
        # Navigate to service
        await browser.browser_navigate({
            "context_id": context_id,
            "url": url,
        })

        # Try to detect different auth mechanisms
        auth_config = await self._detect_form_login(browser, context_id)

        if auth_config:
            return auth_config

        auth_config = await self._detect_oauth(browser, context_id)

        if auth_config:
            return auth_config

        auth_config = await self._detect_api_key(browser, context_id)

        if auth_config:
            return auth_config

        # If nothing detected, use AI analysis
        return await self._ai_auth_analysis(browser, context_id)

    async def _detect_form_login(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any] | None:
        """
        Detect form-based login.

        Returns None if no form login detected.
        """
        try:
            # Check for email/username field
            email_field = await browser.browser_find_element({
                "context_id": context_id,
                "description": "email input or username input field",
            })

            # Check for password field
            password_field = await browser.browser_find_element({
                "context_id": context_id,
                "description": "password input field",
            })

            if email_field.get("found") and password_field.get("found"):
                self._log.info("Form-based login detected")

                # Find submit button
                submit_button = await browser.browser_find_element({
                    "context_id": context_id,
                    "description": "submit button, login button, or sign in button",
                })

                return {
                    "type": "form_login",
                    "email_selector": email_field.get("selector"),
                    "password_selector": password_field.get("selector"),
                    "submit_selector": submit_button.get("selector") if submit_button.get("found") else None,
                }

            return None

        except Exception as e:
            self._log.error("Form login detection failed", error=str(e))
            return None

    async def _detect_oauth(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any] | None:
        """
        Detect OAuth authentication.

        Returns None if no OAuth detected.
        """
        try:
            # Look for OAuth buttons
            oauth_providers = ["Google", "GitHub", "Microsoft", "Facebook", "Apple"]

            for provider in oauth_providers:
                oauth_button = await browser.browser_find_element({
                    "context_id": context_id,
                    "description": f'Sign in with {provider} button or {provider} login button',
                })

                if oauth_button.get("found"):
                    self._log.info("OAuth login detected", provider=provider)

                    return {
                        "type": "oauth",
                        "provider": provider.lower(),
                        "button_selector": oauth_button.get("selector"),
                    }

            return None

        except Exception as e:
            self._log.error("OAuth detection failed", error=str(e))
            return None

    async def _detect_api_key(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any] | None:
        """
        Detect API key authentication.

        Returns None if no API key auth detected.
        """
        try:
            # Look for API key input
            api_key_field = await browser.browser_find_element({
                "context_id": context_id,
                "description": "API key input field or token input field",
            })

            if api_key_field.get("found"):
                self._log.info("API key authentication detected")

                return {
                    "type": "api_key",
                    "api_key_selector": api_key_field.get("selector"),
                }

            return None

        except Exception as e:
            self._log.error("API key detection failed", error=str(e))
            return None

    async def _ai_auth_analysis(
        self,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any]:
        """
        Use AI to analyze authentication mechanism.

        Fallback when pattern matching fails.
        """
        try:
            # Use AI to analyze page
            analysis = await browser.browser_ai_analyze({
                "context_id": context_id,
                "question": """
                Analyze this page and identify:
                1. What authentication method is required?
                2. Where are the login/input fields located?
                3. What actions are needed to authenticate?

                Be specific about element locations and types.
                """,
            })

            self._log.info("AI auth analysis completed", analysis=analysis)

            # Parse AI response to extract auth info
            # This would use LLM parsing to extract structured data

            return {
                "type": "unknown",
                "ai_analysis": analysis,
                "requires_manual_config": True,
            }

        except Exception as e:
            self._log.error("AI auth analysis failed", error=str(e))
            return {
                "type": "unknown",
                "error": str(e),
            }
