"""
JWT (JSON Web Token) authentication with RS256 signing.

Provides JWT generation and automatic refresh capabilities using RSA-SHA256
algorithm for secure authentication with the Owl Browser HTTP server.
"""

from __future__ import annotations

import base64
import json
import time
from pathlib import Path
from typing import Any

from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.asymmetric.types import PrivateKeyTypes

from ..types import JWTConfig


def _base64url_encode(data: bytes) -> str:
    """Base64URL encode bytes to string (no padding)."""
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("utf-8")


def _base64url_decode(data: str) -> bytes:
    """Base64URL decode string to bytes (handles missing padding)."""
    padding_needed = 4 - (len(data) % 4)
    if padding_needed != 4:
        data += "=" * padding_needed
    return base64.urlsafe_b64decode(data)


def _load_private_key(key_or_path: str) -> PrivateKeyTypes:
    """
    Load RSA private key from PEM string or file path.

    Args:
        key_or_path: PEM-encoded private key string or path to key file.

    Returns:
        RSA private key object.

    Raises:
        FileNotFoundError: If the key file does not exist.
        ValueError: If the key format is invalid.
    """
    if "-----BEGIN" in key_or_path or "PRIVATE KEY" in key_or_path:
        key_pem = key_or_path.encode("utf-8")
    else:
        key_path = Path(key_or_path)
        if not key_path.exists():
            raise FileNotFoundError(f"Private key file not found: {key_or_path}")
        key_pem = key_path.read_bytes()

    return serialization.load_pem_private_key(
        key_pem,
        password=None,
        backend=default_backend(),
    )


def _sign_rs256(data: str, private_key: PrivateKeyTypes) -> bytes:
    """Sign data using RS256 (RSA-SHA256)."""
    return private_key.sign(  # type: ignore[union-attr]
        data.encode("utf-8"),
        padding.PKCS1v15(),
        hashes.SHA256(),
    )


class JWTAuth:
    """
    JWT authentication manager with automatic token generation and refresh.

    This class handles JWT token generation using RS256 (RSA-SHA256) signing
    and automatically refreshes tokens before they expire.

    Example:
        ```python
        from owl_browser.auth import JWTAuth
        from owl_browser.types import JWTConfig

        config = JWTConfig(
            private_key_path="/path/to/private.pem",
            expires_in=3600,
            refresh_threshold=300,
            issuer="my-app"
        )

        auth = JWTAuth(config)
        headers = auth.get_headers()
        # headers = {"Authorization": "Bearer <jwt_token>"}
        ```
    """

    __slots__ = (
        "_private_key",
        "_expires_in",
        "_refresh_threshold",
        "_issuer",
        "_subject",
        "_audience",
        "_claims",
        "_current_token",
        "_token_expires_at",
    )

    def __init__(self, config: JWTConfig) -> None:
        """
        Initialize JWT authentication manager.

        Args:
            config: JWT configuration with private key and claims.
        """
        self._private_key = _load_private_key(config.private_key_path)
        self._expires_in = config.expires_in
        self._refresh_threshold = config.refresh_threshold
        self._issuer = config.issuer
        self._subject = config.subject
        self._audience = config.audience
        self._claims = config.claims
        self._current_token: str | None = None
        self._token_expires_at: float = 0

    def get_headers(self) -> dict[str, str]:
        """
        Get authentication headers for HTTP requests.

        Automatically generates or refreshes the token if needed.

        Returns:
            Dictionary containing the Authorization header with JWT.
        """
        return {"Authorization": f"Bearer {self.get_token()}"}

    def get_token(self) -> str:
        """
        Get a valid JWT token, generating a new one if needed.

        Automatically generates a new token if the current one is
        expired or about to expire (within refresh_threshold).

        Returns:
            Valid JWT token string.
        """
        if self._current_token and not self._needs_refresh():
            return self._current_token

        self._current_token = self._generate_token()
        return self._current_token

    def refresh_token(self) -> str:
        """
        Force refresh the token regardless of expiration status.

        Returns:
            New JWT token string.
        """
        self._current_token = self._generate_token()
        return self._current_token

    def get_remaining_time(self) -> int:
        """
        Get the remaining validity time of the current token in seconds.

        Returns:
            Remaining time in seconds, or -1 if no valid token.
        """
        if not self._current_token:
            return -1
        remaining = self._token_expires_at - time.time()
        return int(remaining) if remaining > 0 else -1

    def clear_token(self) -> None:
        """Clear the current token for forced re-authentication."""
        self._current_token = None
        self._token_expires_at = 0

    def _needs_refresh(self) -> bool:
        """Check if the current token should be refreshed."""
        if not self._current_token:
            return True
        remaining = self._token_expires_at - time.time()
        return remaining < self._refresh_threshold

    def _generate_token(self) -> str:
        """Generate a new JWT token."""
        now = int(time.time())
        expires_at = now + self._expires_in
        self._token_expires_at = expires_at

        header = {"alg": "RS256", "typ": "JWT"}

        payload: dict[str, Any] = {
            "iat": now,
            "exp": expires_at,
        }

        if self._issuer:
            payload["iss"] = self._issuer
        if self._subject:
            payload["sub"] = self._subject
        if self._audience:
            payload["aud"] = self._audience
        if self._claims:
            payload.update(self._claims)

        header_b64 = _base64url_encode(
            json.dumps(header, separators=(",", ":")).encode("utf-8")
        )
        payload_b64 = _base64url_encode(
            json.dumps(payload, separators=(",", ":")).encode("utf-8")
        )
        signing_input = f"{header_b64}.{payload_b64}"

        signature = _sign_rs256(signing_input, self._private_key)
        signature_b64 = _base64url_encode(signature)

        return f"{signing_input}.{signature_b64}"


def decode_jwt(token: str) -> dict[str, Any]:
    """
    Decode a JWT token without verifying the signature.

    Useful for debugging or inspecting token contents.

    Args:
        token: JWT token string.

    Returns:
        Dictionary with 'header' and 'payload' keys.

    Raises:
        ValueError: If the token format is invalid.
    """
    parts = token.split(".")
    if len(parts) != 3:
        raise ValueError("Invalid JWT format: expected 3 parts separated by dots")

    header_b64, payload_b64, _ = parts

    try:
        header = json.loads(_base64url_decode(header_b64).decode("utf-8"))
        payload = json.loads(_base64url_decode(payload_b64).decode("utf-8"))
        return {"header": header, "payload": payload}
    except Exception as e:
        raise ValueError(f"Failed to decode JWT: {e}") from e


def is_jwt_expired(token: str, clock_skew: int = 60) -> bool:
    """
    Check if a JWT token is expired.

    Args:
        token: JWT token string.
        clock_skew: Allowed clock skew in seconds (default: 60).

    Returns:
        True if the token is expired.
    """
    try:
        result = decode_jwt(token)
        exp = result["payload"].get("exp")
        if exp is None:
            return False
        now = int(time.time())
        return bool(exp < now - clock_skew)
    except Exception:
        return True
