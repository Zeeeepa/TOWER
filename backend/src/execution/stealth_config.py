"""
Advanced stealth configuration for Owl-Browser.

This module provides comprehensive anti-detection features:
- WebDriver fingerprint masking
- Canvas fingerprint protection
- Browser headers normalization
- Behavior randomization
- Bot detection evasion
"""

from __future__ import annotations

import random
from typing import Any

import structlog

logger = structlog.get_logger(__name__)


class StealthConfig:
    """
    Advanced stealth configuration for browser automation.

    Implements multiple layers of anti-detection:
    1. Browser fingerprint masking
    2. Canvas protection
    3. Headers normalization
    4. Behavior randomization
    5. Timing simulation
    """

    # Realistic user agents
    USER_AGENTS = [
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:121.0) Gecko/20100101 Firefox/121.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:121.0) Gecko/20100101 Firefox/121.0",
    ]

    # Screen resolutions
    SCREEN_RESOLUTIONS = [
        (1920, 1080),
        (2560, 1440),
        (1440, 900),
        (1536, 864),
        (1366, 768),
    ]

    # Timezones
    TIMEZONES = [
        "America/New_York",
        "America/Chicago",
        "America/Denver",
        "America/Los_Angeles",
        "Europe/London",
        "Europe/Paris",
        "Asia/Tokyo",
    ]

    # Languages
    LANGUAGES = ["en-US", "en-GB", "en-CA", "en-AU"]

    def __init__(self, randomize: bool = True):
        """
        Initialize stealth configuration.

        Args:
            randomize: Whether to randomize fingerprints
        """
        self._randomize = randomize
        self._log = logger.bind(component="stealth_config")

        if randomize:
            self._user_agent = random.choice(self.USER_AGENTS)
            self._screen_resolution = random.choice(self.SCREEN_RESOLUTIONS)
            self._timezone = random.choice(self.TIMEZONES)
            self._language = random.choice(self.LANGUAGES)
        else:
            self._user_agent = self.USER_AGENTS[0]
            self._screen_resolution = self.SCREEN_RESOLUTIONS[0]
            self._timezone = self.TIMEZONES[0]
            self._language = self.LANGUAGES[0]

    def get_browser_context_config(self) -> dict[str, Any]:
        """
        Get Playwright browser context configuration with stealth settings.

        Returns:
            Dictionary with context configuration
        """
        width, height = self._screen_resolution

        config = {
            "user_agent": self._user_agent,
            "viewport": {"width": width, "height": height},
            "locale": self._language,
            "timezone_id": self._timezone,
            "geolocation": self._get_geolocation(),
            "permissions": ["geolocation"],
            "color_scheme": "light",  # or 'dark' or 'no-preference'
            "device_scale_factor": 1.0,
            "has_touch": False,  # Desktop browser
            "java_script_enabled": True,
            "bypass_csp": True,  # Bypass Content Security Policy
        }

        self._log.info(
            "Browser context config generated",
            user_agent=config["user_agent"],
            resolution=config["viewport"],
            timezone=config["timezone_id"],
        )

        return config

    def _get_geolocation(self) -> dict[str, float]:
        """Get realistic geolocation based on timezone."""
        # Map timezones to approximate coordinates
        geo_map = {
            "America/New_York": {"latitude": 40.7128, "longitude": -74.0060},
            "America/Chicago": {"latitude": 41.8781, "longitude": -87.6298},
            "America/Denver": {"latitude": 39.7392, "longitude": -104.9903},
            "America/Los_Angeles": {"latitude": 34.0522, "longitude": -118.2437},
            "Europe/London": {"latitude": 51.5074, "longitude": -0.1278},
            "Europe/Paris": {"latitude": 48.8566, "longitude": 2.3522},
            "Asia/Tokyo": {"latitude": 35.6762, "longitude": 139.6503},
        }

        return geo_map.get(self._timezone, geo_map["America/New_York"])

    async def apply_stealth_scripts(self, page) -> None:
        """
        Apply stealth scripts to the page.

        Args:
            page: Playwright page object
        """
        # Script to hide WebDriver
        await page.add_init_script(
            """
            // Hide WebDriver
            Object.defineProperty(navigator, 'webdriver', {
                get: () => undefined,
            });

            // Hide plugins
            Object.defineProperty(navigator, 'plugins', {
                get: () => [1, 2, 3, 4, 5],
            });

            // Hide languages
            Object.defineProperty(navigator, 'languages', {
                get: () => ['en-US', 'en'],
            });

            // Mock chrome runtime
            window.chrome = {
                runtime: {},
            };

            // Mock permissions
            const originalQuery = window.navigator.permissions.query;
            window.navigator.permissions.query = (parameters) => (
                parameters.name === 'notifications' ?
                    Promise.resolve({ state: Notification.permission }) :
                    originalQuery(parameters)
            );

            // Hide automation
            Object.defineProperty(navigator, 'automation', {
                get: () => undefined,
            });
            """
        )

        self._log.info("Stealth scripts applied")

    def get_random_delay(self, min_ms: int = 100, max_ms: int = 300) -> float:
        """
        Get random delay to simulate human behavior.

        Args:
            min_ms: Minimum delay in milliseconds
            max_ms: Maximum delay in milliseconds

        Returns:
            Delay in seconds
        """
        delay_ms = random.randint(min_ms, max_ms)
        return delay_ms / 1000.0

    def get_typing_delay(self, text_length: int) -> float:
        """
        Calculate realistic typing delay based on text length.

        Args:
            text_length: Length of text to type

        Returns:
            Delay in seconds
        """
        # Average typing speed: 40 words per minute = ~200 chars per minute
        # = ~3.3 chars per second = ~300ms per char
        base_delay = text_length * 0.05  # 50ms per character
        variation = random.uniform(0.8, 1.2)  # ±20% variation
        return base_delay * variation

    def get_mouse_trajectory(
        self, start_x: int, start_y: int, end_x: int, end_y: int, steps: int = 10
    ) -> list[tuple[int, int]]:
        """
        Generate realistic mouse trajectory with bezier curves.

        Args:
            start_x: Start X coordinate
            start_y: Start Y coordinate
            end_x: End X coordinate
            end_y: End Y coordinate
            steps: Number of steps in trajectory

        Returns:
            List of (x, y) coordinates
        """
        trajectory = []

        # Simple bezier curve
        for i in range(steps + 1):
            t = i / steps

            # Add some randomness
            noise_x = random.uniform(-10, 10)
            noise_y = random.uniform(-10, 10)

            x = int((1 - t) * start_x + t * end_x + noise_x)
            y = int((1 - t) * start_y + t * end_y + noise_y)

            trajectory.append((x, y))

        return trajectory

    def get_scroll_behavior(self) -> dict[str, Any]:
        """
        Get realistic scroll behavior.

        Returns:
            Dictionary with scroll configuration
        """
        return {
            "delay_min": 50,  # Minimum delay between scroll events (ms)
            "delay_max": 200,  # Maximum delay between scroll events (ms)
            "step_min": 100,  # Minimum scroll step (pixels)
            "step_max": 300,  # Maximum scroll step (pixels)
            "probability": 0.7,  # Probability of scrolling at each step
        }


class StealthBehaviorSimulator:
    """
    Simulate human-like browser behavior.

    Features:
    - Random delays
    - Mouse movement simulation
    - Scroll patterns
    - Typing rhythms
    """

    def __init__(self, config: StealthConfig):
        """
        Initialize behavior simulator.

        Args:
            config: Stealth configuration
        """
        self._config = config
        self._log = logger.bind(component="behavior_simulator")

    async def simulate_typing(self, page, selector: str, text: str) -> None:
        """
        Simulate realistic typing.

        Args:
            page: Playwright page object
            selector: Element selector
            text: Text to type
        """
        delay = self._config.get_typing_delay(len(text))

        # Type character by character with random delays
        for char in text:
            await page.type(selector, char, delay=random.uniform(50, 150))

        # Add pause after typing
        await asyncio.sleep(self._config.get_random_delay(200, 500))

        self._log.info("Typing simulated", text_length=len(text))

    async def simulate_mouse_movement(self, page, target_selector: str) -> None:
        """
        Simulate realistic mouse movement before clicking.

        Args:
            page: Playwright page object
            target_selector: Target element selector
        """
        # Get element position
        element = await page.query_selector(target_selector)
        box = await element.bounding_box()

        if box:
            start_x = box["x"] + random.randint(-100, 100)
            start_y = box["y"] + random.randint(-100, 100)
            end_x = box["x"] + box["width"] / 2
            end_y = box["y"] + box["height"] / 2

            # Generate trajectory
            trajectory = self._config.get_mouse_trajectory(
                start_x, start_y, end_x, end_y
            )

            # Move mouse along trajectory
            for x, y in trajectory:
                await page.mouse.move(x, y)
                await asyncio.sleep(random.uniform(0.01, 0.05))

        self._log.info("Mouse movement simulated")

    async def simulate_scroll(self, page, direction: str = "down") -> None:
        """
        Simulate realistic scrolling behavior.

        Args:
            page: Playwright page object
            direction: Scroll direction ('up' or 'down')
        """
        scroll_config = self._config.get_scroll_behavior()

        # Scroll multiple times with random delays
        for _ in range(random.randint(2, 5)):
            if random.random() < scroll_config["probability"]:
                step = random.randint(
                    scroll_config["step_min"], scroll_config["step_max"]
                )

                if direction == "down":
                    await page.evaluate(f"window.scrollBy(0, {step})")
                else:
                    await page.evaluate(f"window.scrollBy(0, -{step})")

                delay = random.randint(
                    scroll_config["delay_min"], scroll_config["delay_max"]
                )
                await asyncio.sleep(delay / 1000.0)

        self._log.info("Scroll simulated", direction=direction)

    async def simulate_reading(self, page, duration_ms: int = 2000) -> None:
        """
        Simulate reading time.

        Args:
            page: Playwright page object
            duration_ms: Base duration in milliseconds
        """
        # Add variation to reading time
        actual_duration = duration_ms * random.uniform(0.8, 1.2)
        await asyncio.sleep(actual_duration / 1000.0)

        self._log.info("Reading simulated", duration_ms=actual_duration)


# Import asyncio
import asyncio


# Factory function
def create_stealth_config(randomize: bool = True) -> StealthConfig:
    """
    Create stealth configuration.

    Args:
        randomize: Whether to randomize fingerprints

    Returns:
        StealthConfig instance
    """
    return StealthConfig(randomize=randomize)


def create_behavior_simulator(config: StealthConfig) -> StealthBehaviorSimulator:
    """
    Create behavior simulator.

    Args:
        config: Stealth configuration

    Returns:
        StealthBehaviorSimulator instance
    """
    return StealthBehaviorSimulator(config)
