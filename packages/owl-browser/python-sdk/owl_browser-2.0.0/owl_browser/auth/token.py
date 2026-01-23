"""
Simple bearer token authentication.

Provides a minimal authentication implementation using static bearer tokens.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class TokenAuth:
    """
    Simple bearer token authentication.

    Provides authentication using a static bearer token that is included
    in the Authorization header of each request.

    Example:
        ```python
        auth = TokenAuth(token="your-secret-token")
        headers = auth.get_headers()
        # headers = {"Authorization": "Bearer your-secret-token"}
        ```
    """

    token: str

    def get_headers(self) -> dict[str, str]:
        """
        Get authentication headers for HTTP requests.

        Returns:
            Dictionary containing the Authorization header with bearer token.
        """
        return {"Authorization": f"Bearer {self.token}"}

    def get_token(self) -> str:
        """
        Get the raw token value.

        Returns:
            The bearer token string.
        """
        return self.token
