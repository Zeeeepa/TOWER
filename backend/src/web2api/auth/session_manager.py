"""
Browser session management with cookie persistence.

Handles session creation, restoration, and validation for service authentication.
Core component of Web2API for maintaining authenticated sessions.
"""

from __future__ import annotations

import json
import time
from typing import Any, TYPE_CHECKING

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class SessionManager:
    """
    Manages browser session lifecycle for services.

    Features:
    - Session creation and tracking
    - Cookie persistence using browser_get_cookies and browser_set_cookie
    - Session validation and expiry
    - Automatic session refresh
    - Database-backed cookie storage for persistence across restarts
    """

    # Session expiry time in seconds (24 hours default)
    DEFAULT_SESSION_EXPIRY = 24 * 60 * 60
    
    def __init__(self, session_expiry: int = DEFAULT_SESSION_EXPIRY) -> None:
        """Initialize session manager.
        
        Args:
            session_expiry: Session expiry time in seconds
        """
        self._sessions: dict[str, dict[str, Any]] = {}
        self._session_expiry = session_expiry
        self._log = logger.bind(component="session_manager")

    async def create_session(
        self,
        service_id: str,
        browser: Browser,
        context_id: str,
    ) -> dict[str, Any]:
        """
        Create a new browser session for a service.

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            Session information dictionary
        """
        session_id = f"{service_id}_{int(time.time())}"

        session = {
            "session_id": session_id,
            "service_id": service_id,
            "context_id": context_id,
            "created_at": time.time(),
            "last_used_at": time.time(),
            "status": "active",
        }

        self._sessions[session_id] = session

        self._log.info(
            "Session created",
            session_id=session_id,
            service_id=service_id,
        )

        return session

    async def save_session(
        self,
        service_id: str,
        browser: Browser,
        context_id: str,
        storage,
        service_url: str | None = None,
    ) -> dict[str, Any] | None:
        """
        Save session cookies for persistence.

        Uses browser_get_cookies to extract authenticated session cookies.
        Cookies are stored in the database for persistence across restarts.

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            context_id: Browser context ID
            storage: Storage backend to persist cookies
            service_url: Optional service URL to filter cookies

        Returns:
            Saved cookies data
        """
        try:
            # Use Owl-Browser command to get all cookies
            cookies_result = await browser.browser_get_cookies({
                "context_id": context_id,
            })

            cookies = cookies_result.get("cookies", [])

            if not cookies:
                self._log.warning(
                    "No cookies found for session",
                    service_id=service_id,
                )
                return None

            # Filter cookies by domain if URL provided
            if service_url:
                from urllib.parse import urlparse
                domain = urlparse(service_url).netloc
                cookies = [
                    c for c in cookies 
                    if domain in c.get("domain", "") or c.get("domain", "").endswith(domain)
                ]

            # Prepare cookie data for storage
            cookie_data = {
                "service_id": service_id,
                "cookies": cookies,
                "saved_at": time.time(),
                "expires_at": time.time() + self._session_expiry,
                "url": service_url,
            }

            # Store cookies in database
            await storage.save_session_cookies(service_id, cookie_data)

            # Update in-memory session tracking
            session_id = f"{service_id}_session"
            self._sessions[session_id] = {
                "session_id": session_id,
                "service_id": service_id,
                "context_id": context_id,
                "created_at": time.time(),
                "last_used_at": time.time(),
                "status": "active",
                "cookie_count": len(cookies),
            }

            self._log.info(
                "Session cookies saved",
                service_id=service_id,
                cookie_count=len(cookies),
            )

            return cookie_data

        except Exception as e:
            self._log.error(
                "Failed to save session cookies",
                service_id=service_id,
                error=str(e),
            )
            return None

    async def restore_session(
        self,
        service_id: str,
        browser: Browser,
        context_id: str,
        storage,
        service_url: str | None = None,
    ) -> bool:
        """
        Restore saved session cookies.

        Uses browser_set_cookie to restore authenticated session.
        Automatically checks for session expiry.

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            context_id: Browser context ID
            storage: Storage backend to retrieve cookies
            service_url: Optional service URL to navigate to after restoring

        Returns:
            True if session was successfully restored
        """
        try:
            # Retrieve cookies from storage
            cookie_data = await storage.get_session_cookies(service_id)

            if not cookie_data or not cookie_data.get("cookies"):
                self._log.info(
                    "No saved cookies found for service",
                    service_id=service_id,
                )
                return False
            
            # Check if session has expired
            expires_at = cookie_data.get("expires_at", 0)
            if expires_at and time.time() > expires_at:
                self._log.info(
                    "Session cookies have expired",
                    service_id=service_id,
                    expired_seconds_ago=int(time.time() - expires_at),
                )
                # Clear expired cookies from storage
                await storage.delete_session_cookies(service_id)
                return False

            cookies = cookie_data["cookies"]
            
            # Navigate to service URL first if provided (cookies need to be set on the domain)
            if service_url:
                try:
                    await browser.browser_navigate({
                        "context_id": context_id,
                        "url": service_url,
                    })
                    # Wait for initial page load
                    await browser.browser_wait_for_load({
                        "context_id": context_id,
                        "state": "domcontentloaded",
                        "timeout": 30000,
                    })
                except Exception as e:
                    self._log.warning(
                        "Failed to navigate before setting cookies",
                        error=str(e),
                    )

            # Use Owl-Browser command to set cookies
            await browser.browser_set_cookie({
                "context_id": context_id,
                "cookies": cookies,
            })
            
            # Refresh the page to apply cookies
            if service_url:
                try:
                    await browser.browser_reload({
                        "context_id": context_id,
                    })
                    await browser.browser_wait_for_load({
                        "context_id": context_id,
                        "state": "domcontentloaded",
                        "timeout": 30000,
                    })
                except Exception as e:
                    self._log.debug("Reload after cookie set failed", error=str(e))

            # Update in-memory session tracking
            session_id = f"{service_id}_session"
            self._sessions[session_id] = {
                "session_id": session_id,
                "service_id": service_id,
                "context_id": context_id,
                "restored_at": time.time(),
                "last_used_at": time.time(),
                "status": "active",
                "cookie_count": len(cookies),
            }

            self._log.info(
                "Session cookies restored",
                service_id=service_id,
                cookie_count=len(cookies),
            )

            return True

        except Exception as e:
            self._log.error(
                "Failed to restore session cookies",
                service_id=service_id,
                error=str(e),
            )
            return False
    
    async def is_session_valid(
        self,
        service_id: str,
        storage,
    ) -> bool:
        """
        Check if a saved session exists and is not expired.
        
        Args:
            service_id: Service identifier
            storage: Storage backend
            
        Returns:
            True if valid session exists
        """
        try:
            cookie_data = await storage.get_session_cookies(service_id)
            
            if not cookie_data or not cookie_data.get("cookies"):
                return False
            
            expires_at = cookie_data.get("expires_at", 0)
            if expires_at and time.time() > expires_at:
                return False
            
            return True
            
        except Exception:
            return False

    async def validate_session(
        self,
        service_id: str,
        service_url: str,
        browser: Browser,
        context_id: str,
    ) -> bool:
        """
        Validate if saved session is still valid.

        Navigates to service and checks if still authenticated.

        Args:
            service_id: Service identifier
            service_url: Service URL
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            True if session is still valid
        """
        try:
            # Navigate to service
            await browser.browser_navigate({
                "context_id": context_id,
                "url": service_url,
            })

            # Get current URL
            current_url_result = await browser.browser_get_current_url({
                "context_id": context_id,
            })

            current_url = current_url_result.get("url", "")

            # Check if redirected to login page
            if "login" in current_url.lower() or "signin" in current_url.lower():
                self._log.info(
                    "Session expired - redirected to login",
                    service_id=service_id,
                    current_url=current_url,
                )
                return False

            # Additional check: look for login form elements
            has_login_form = await browser.browser_find_element({
                "context_id": context_id,
                "description": "password input field or login form",
            })

            if has_login_form.get("found"):
                self._log.info(
                    "Session expired - login form detected",
                    service_id=service_id,
                )
                return False

            self._log.info(
                "Session valid",
                service_id=service_id,
            )

            return True

        except Exception as e:
            self._log.error(
                "Session validation failed",
                service_id=service_id,
                error=str(e),
            )
            return False

    async def expire_session(self, session_id: str) -> None:
        """
        Mark a session as expired.

        Args:
            session_id: Session identifier
        """
        if session_id in self._sessions:
            self._sessions[session_id]["status"] = "expired"
            self._log.info("Session expired", session_id=session_id)

    def get_session(self, session_id: str) -> dict[str, Any] | None:
        """Get session information by ID."""
        return self._sessions.get(session_id)

    def list_sessions(self, service_id: str | None = None) -> list[dict[str, Any]]:
        """List all sessions, optionally filtered by service."""
        sessions = list(self._sessions.values())

        if service_id:
            sessions = [s for s in sessions if s["service_id"] == service_id]

        return sessions
