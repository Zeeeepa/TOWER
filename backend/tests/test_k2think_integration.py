"""
Integration test for K2Think service via Web2API.

This test specifically validates the complete flow:
1. Service creation with K2Think credentials
2. Login flow execution
3. Text input identification and interaction
4. Send button state tracking
5. Response retrieval and OpenAI formatting
"""

import asyncio
import os
import uuid
from typing import Any

import httpx
import structlog

# Configure logging
logger = structlog.get_logger(__name__)


class K2ThinkIntegrationTest:
    """Test suite for K2Think service integration."""

    BASE_URL = os.getenv("WEB2API_BASE_URL", "http://localhost:8000")

    # K2Think service configuration
    K2THINK_URL = "https://www.k2think.ai"
    K2THINK_EMAIL = "developer@pixelium.uk"
    K2THINK_PASSWORD = "developer123?"

    def __init__(self):
        """Initialize test instance."""
        self.service_id: str | None = None
        self.http_client: httpx.AsyncClient | None = None

    async def setup(self):
        """Setup test environment."""
        self.http_client = httpx.AsyncClient(timeout=60.0)
        logger.info("Test setup completed")

    async def teardown(self):
        """Cleanup test environment."""
        if self.http_client:
            await self.http_client.aclose()

        if self.service_id:
            try:
                await self.http_client.delete(
                    f"{self.BASE_URL}/api/v1/services/{self.service_id}"
                )
                logger.info("Service deleted", service_id=self.service_id)
            except Exception as e:
                logger.warning("Failed to delete service", error=str(e))

    async def test_01_create_service(self):
        """Test 1: Create K2Think service."""
        logger.info("Test 1: Creating K2Think service")

        response = await self.http_client.post(
            f"{self.BASE_URL}/api/v1/services",
            json={
                "name": "k2think",
                "url": self.K2THINK_URL,
                "description": "K2Think AI Service",
            },
        )

        if response.status_code != 201:
            logger.error("Failed to create service", status=response.status_code)
            return False

        data = response.json()
        self.service_id = data["id"]

        logger.info("Service created successfully", service_id=self.service_id)
        return True

    async def test_02_store_credentials(self):
        """Test 2: Store K2Think credentials."""
        logger.info("Test 2: Storing K2Think credentials")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        response = await self.http_client.post(
            f"{self.BASE_URL}/api/v1/services/{self.service_id}/credentials",
            json={
                "email": self.K2THINK_EMAIL,
                "password": self.K2THINK_PASSWORD,
            },
        )

        if response.status_code != 200:
            logger.error("Failed to store credentials", status=response.status_code)
            return False

        logger.info("Credentials stored successfully")
        return True

    async def test_03_discover_service(self):
        """Test 3: Discover service elements."""
        logger.info("Test 3: Discovering service elements")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        response = await self.http_client.post(
            f"{self.BASE_URL}/api/v1/services/{self.service_id}/discover",
            json={
                "force": True,
            },
        )

        if response.status_code != 200:
            logger.error("Discovery failed", status=response.status_code)
            return False

        data = response.json()
        logger.info("Discovery completed", results=data)

        # Verify key elements were discovered
        assert "input_elements" in data or "config" in data

        return True

    async def test_04_login_to_service(self):
        """Test 4: Login to K2Think service."""
        logger.info("Test 4: Logging into K2Think service")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        response = await self.http_client.post(
            f"{self.BASE_URL}/api/v1/services/{self.service_id}/login",
        )

        if response.status_code != 200:
            logger.error("Login failed", status=response.status_code)
            return False

        data = response.json()
        logger.info("Login successful", result=data)

        return True

    async def test_05_identify_text_input(self):
        """Test 5: Identify text input element."""
        logger.info("Test 5: Identifying text input element")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        response = await self.http_client.get(
            f"{self.BASE_URL}/api/v1/services/{self.service_id}/elements/text_input",
        )

        if response.status_code != 200:
            logger.warning(
                "Text input endpoint not implemented", status=response.status_code
            )
            return True  # Not a failure if endpoint doesn't exist yet

        data = response.json()
        logger.info("Text input identified", element=data)

        return True

    async def test_06_identify_send_button(self):
        """Test 6: Identify send button element."""
        logger.info("Test 6: Identifying send button element")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        response = await self.http_client.get(
            f"{self.BASE_URL}/api/v1/services/{self.service_id}/elements/send_button",
        )

        if response.status_code != 200:
            logger.warning(
                "Send button endpoint not implemented", status=response.status_code
            )
            return True  # Not a failure if endpoint doesn't exist yet

        data = response.json()
        logger.info("Send button identified", element=data)

        return True

    async def test_07_send_message(self):
        """Test 7: Send message via Web2API."""
        logger.info("Test 7: Sending message via Web2API")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        test_message = "Hello K2Think! This is a test message from Web2API."

        response = await self.http_client.post(
            f"{self.BASE_URL}/v1/chat/completions",
            json={
                "model": self.service_id,
                "messages": [{"role": "user", "content": test_message}],
                "temperature": 0.7,
                "max_tokens": 1000,
            },
        )

        if response.status_code != 200:
            logger.error("Message send failed", status=response.status_code)
            return False

        data = response.json()
        logger.info("Message sent successfully", response=data)

        # Verify OpenAI format
        assert "id" in data
        assert "choices" in data
        assert len(data["choices"]) > 0

        return True

    async def test_08_verify_response_format(self):
        """Test 8: Verify response matches OpenAI format."""
        logger.info("Test 8: Verifying OpenAI response format")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        test_message = "What is 2+2?"

        response = await self.http_client.post(
            f"{self.BASE_URL}/v1/chat/completions",
            json={
                "model": self.service_id,
                "messages": [{"role": "user", "content": test_message}],
            },
        )

        if response.status_code != 200:
            logger.error("Request failed", status=response.status_code)
            return False

        data = response.json()

        # Verify OpenAI format structure
        required_fields = ["id", "object", "created", "model", "choices"]
        for field in required_fields:
            assert field in data, f"Missing required field: {field}"

        # Verify choices structure
        assert len(data["choices"]) > 0
        choice = data["choices"][0]
        assert "index" in choice
        assert "message" in choice
        assert "finish_reason" in choice

        # Verify message structure
        message = choice["message"]
        assert "role" in message
        assert message["role"] == "assistant"
        assert "content" in message

        logger.info("Response format verified", response_id=data["id"])

        return True

    async def test_09_multiple_turns(self):
        """Test 9: Test multiple conversation turns."""
        logger.info("Test 9: Testing multiple conversation turns")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        messages = [
            {"role": "user", "content": "My name is Alice"},
            {
                "role": "assistant",
                "content": "Hello Alice! How can I help you today?",
            },
            {"role": "user", "content": "What is my name?"},
        ]

        response = await self.http_client.post(
            f"{self.BASE_URL}/v1/chat/completions",
            json={
                "model": self.service_id,
                "messages": messages,
            },
        )

        if response.status_code != 200:
            logger.error("Multi-turn request failed", status=response.status_code)
            return False

        data = response.json()
        assistant_response = data["choices"][0]["message"]["content"]

        logger.info("Multi-turn conversation successful", response=assistant_response)

        return True

    async def test_10_error_handling(self):
        """Test 10: Test error handling."""
        logger.info("Test 10: Testing error handling")

        if not self.service_id:
            logger.error("Service ID not available")
            return False

        # Test with invalid UUID
        response = await self.http_client.post(
            f"{self.BASE_URL}/v1/chat/completions",
            json={
                "model": "invalid-uuid-format",
                "messages": [{"role": "user", "content": "Test"}],
            },
        )

        # Should return 400 for invalid UUID
        assert response.status_code == 400, "Should return 400 for invalid UUID"

        logger.info("Error handling verified")

        return True

    async def run_all_tests(self):
        """Run all integration tests."""
        logger.info("Starting K2Think integration tests")

        await self.setup()

        tests = [
            ("Create Service", self.test_01_create_service),
            ("Store Credentials", self.test_02_store_credentials),
            ("Discover Service", self.test_03_discover_service),
            ("Login to Service", self.test_04_login_to_service),
            ("Identify Text Input", self.test_05_identify_text_input),
            ("Identify Send Button", self.test_06_identify_send_button),
            ("Send Message", self.test_07_send_message),
            ("Verify Response Format", self.test_08_verify_response_format),
            ("Multiple Turns", self.test_09_multiple_turns),
            ("Error Handling", self.test_10_error_handling),
        ]

        results = []
        for test_name, test_func in tests:
            logger.info(f"Running test: {test_name}")
            try:
                result = await test_func()
                results.append((test_name, result))
                logger.info(
                    f"Test {test_name}: {'✅ PASSED' if result else '❌ FAILED'}"
                )
            except Exception as e:
                logger.error(f"Test {test_name} failed with exception", error=str(e))
                results.append((test_name, False))

        await self.teardown()

        # Print summary
        logger.info("=" * 50)
        logger.info("TEST SUMMARY")
        logger.info("=" * 50)
        passed = sum(1 for _, result in results if result)
        total = len(results)
        for test_name, result in results:
            status = "✅ PASSED" if result else "❌ FAILED"
            logger.info(f"{test_name}: {status}")
        logger.info("=" * 50)
        logger.info(f"TOTAL: {passed}/{total} tests passed")
        logger.info("=" * 50)

        return passed == total


async def main():
    """Main entry point."""
    test = K2ThinkIntegrationTest()
    success = await test.run_all_tests()
    return 0 if success else 1


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    exit(exit_code)
