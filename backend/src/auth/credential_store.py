"""
Encrypted credential storage for service authentication.

Uses Fernet symmetric encryption for secure at-rest storage.
"""

from __future__ import annotations

import os
from typing import Any

from cryptography.fernet import Fernet
import structlog

logger = structlog.get_logger(__name__)


class CredentialStore:
    """
    Manages encrypted storage of service credentials.

    Supports multiple credential types:
    - form_login: Email/username + password
    - api_key: API key or token
    - oauth: OAuth tokens
    """

    def __init__(self, encryption_key: bytes | None = None) -> None:
        """
        Initialize credential store.

        Args:
            encryption_key: Fernet encryption key. If None, reads from CREDENTIAL_ENCRYPTION_KEY env var
                           or generates a new key (WARNING: Generated keys are lost on restart!)
        """
        if encryption_key is None:
            encryption_key_str = os.environ.get("CREDENTIAL_ENCRYPTION_KEY")
            if encryption_key_str:
                encryption_key = encryption_key_str.encode()
            else:
                # Generate a warning - this should be set in production
                logger.warning(
                    "No CREDENTIAL_ENCRYPTION_KEY provided, generating temporary key. "
                    "This key will be lost on restart!"
                )
                encryption_key = Fernet.generate_key()

        self._cipher = Fernet(encryption_key)
        self._log = logger.bind(component="credential_store")

    def encrypt(self, plaintext: str) -> str:
        """
        Encrypt plaintext string.

        Args:
            plaintext: String to encrypt

        Returns:
            Encrypted string (base64-encoded)
        """
        try:
            encrypted_bytes = self._cipher.encrypt(plaintext.encode())
            return encrypted_bytes.decode()
        except Exception as e:
            self._log.error("Encryption failed", error=str(e))
            raise

    def decrypt(self, encrypted_text: str) -> str:
        """
        Decrypt encrypted string.

        Args:
            encrypted_text: Encrypted string to decrypt

        Returns:
            Decrypted plaintext string
        """
        try:
            decrypted_bytes = self._cipher.decrypt(encrypted_text.encode())
            return decrypted_bytes.decode()
        except Exception as e:
            self._log.error("Decryption failed", error=str(e))
            raise

    def encrypt_credentials(
        self,
        email: str | None = None,
        password: str | None = None,
        api_key: str | None = None,
        **extra_fields: Any,
    ) -> dict[str, str | None]:
        """
        Encrypt credential fields.

        Args:
            email: Email/username
            password: Password
            api_key: API key
            **extra_fields: Additional fields to encrypt

        Returns:
            Dictionary with encrypted values
        """
        result = {}

        if email:
            result["encrypted_email"] = self.encrypt(email)
        if password:
            result["encrypted_password"] = self.encrypt(password)
        if api_key:
            result["encrypted_api_key"] = self.encrypt(api_key)

        # Encrypt any extra fields
        for key, value in extra_fields.items():
            if isinstance(value, str):
                result[f"encrypted_{key}"] = self.encrypt(value)

        return result

    def decrypt_credentials(
        self,
        encrypted_email: str | None = None,
        encrypted_password: str | None = None,
        encrypted_api_key: str | None = None,
        **extra_fields: str,
    ) -> dict[str, str | None]:
        """
        Decrypt credential fields.

        Args:
            encrypted_email: Encrypted email
            encrypted_password: Encrypted password
            encrypted_api_key: Encrypted API key
            **extra_fields: Additional encrypted fields

        Returns:
            Dictionary with decrypted plaintext values
        """
        result = {}

        if encrypted_email:
            result["email"] = self.decrypt(encrypted_email)
        if encrypted_password:
            result["password"] = self.decrypt(encrypted_password)
        if encrypted_api_key:
            result["api_key"] = self.decrypt(encrypted_api_key)

        # Decrypt any extra fields
        for key, value in extra_fields.items():
            if key.startswith("encrypted_") and isinstance(value, str):
                plain_key = key[len("encrypted_"):]
                result[plain_key] = self.decrypt(value)

        return result
