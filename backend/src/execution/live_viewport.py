"""
Live viewport streaming using Owl-Browser start_live_stream.

Streams browser viewport to frontend during discovery and execution.
"""

from __future__ import annotations

import asyncio
import base64
from typing import Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class LiveViewportManager:
    """
    Manages live viewport streaming for services.

    Uses Owl-Browser commands:
    - start_live_stream: Start streaming viewport
    - get_live_frame: Get single frame
    - stop_live_stream: Stop streaming
    """

    def __init__(self) -> None:
        """Initialize live viewport manager."""
        self._active_streams: dict[str, dict[str, Any]] = {}
        self._log = logger.bind(component="live_viewport")

    async def start_streaming(
        self,
        service_id: str,
        browser: Browser,
        context_id: str,
        websocket_handler: Any = None,
        quality: str = "medium",
        fps: int = 5,
    ) -> str:
        """
        Start live viewport stream for a service.

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            context_id: Browser context ID
            websocket_handler: Optional WebSocket handler for frame streaming
            quality: Stream quality (low, medium, high)
            fps: Frames per second

        Returns:
            Stream ID
        """
        try:
            # Start live stream using Owl-Browser
            stream_result = await browser.start_live_stream({
                "context_id": context_id,
                "quality": quality,
                "fps": fps,
            })

            stream_id = stream_result.get("stream_id", f"stream_{service_id}")

            # Store stream info
            self._active_streams[stream_id] = {
                "service_id": service_id,
                "context_id": context_id,
                "started_at": asyncio.get_event_loop().time(),
                "quality": quality,
                "fps": fps,
            }

            self._log.info(
                "Live viewport stream started",
                stream_id=stream_id,
                service_id=service_id,
            )

            # Start background task to stream frames
            if websocket_handler:
                asyncio.create_task(
                    self._stream_frames(
                        stream_id,
                        browser,
                        websocket_handler,
                    )
                )

            return stream_id

        except Exception as e:
            self._log.error(
                "Failed to start live stream",
                service_id=service_id,
                error=str(e),
            )
            raise

    async def _stream_frames(
        self,
        stream_id: str,
        browser: Browser,
        websocket_handler: Any,
    ) -> None:
        """
        Background task to continuously stream frames.

        Args:
            stream_id: Stream identifier
            browser: Owl-Browser instance
            websocket_handler: WebSocket handler for sending frames
        """
        if stream_id not in self._active_streams:
            return

        stream_info = self._active_streams[stream_id]
        service_id = stream_info["service_id"]
        fps = stream_info["fps"]
        interval = 1.0 / fps

        self._log.debug("Frame streaming started", stream_id=stream_id)

        while stream_id in self._active_streams:
            try:
                # Get current frame
                frame_result = await browser.get_live_frame({
                    "stream_id": stream_id,
                })

                frame_data = frame_result.get("frame")

                if frame_data:
                    # Send to frontend via WebSocket
                    await websocket_handler.send_stream_frame(
                        service_id,
                        frame_data,
                    )

                # Wait before next frame
                await asyncio.sleep(interval)

            except asyncio.CancelledError:
                break
            except Exception as e:
                self._log.error(
                    "Frame streaming error",
                    stream_id=stream_id,
                    error=str(e),
                )
                await asyncio.sleep(1)  # Brief pause before retry

        self._log.debug("Frame streaming stopped", stream_id=stream_id)

    async def stop_streaming(
        self,
        stream_id: str,
        browser: Browser,
    ) -> None:
        """
        Stop live viewport stream.

        Args:
            stream_id: Stream identifier
            browser: Owl-Browser instance
        """
        try:
            # Stop live stream using Owl-Browser
            await browser.stop_live_stream({"stream_id": stream_id})

            # Remove from active streams
            if stream_id in self._active_streams:
                del self._active_streams[stream_id]

            self._log.info("Live viewport stream stopped", stream_id=stream_id)

        except Exception as e:
            self._log.error(
                "Failed to stop live stream",
                stream_id=stream_id,
                error=str(e),
            )

    async def get_single_frame(
        self,
        service_id: str,
        browser: Browser,
        context_id: str,
    ) -> str | None:
        """
        Get a single viewport frame (without streaming).

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            Base64-encoded frame data
        """
        try:
            # Take screenshot
            screenshot_result = await browser.browser_screenshot({
                "context_id": context_id,
            })

            frame_data = screenshot_result.get("data")

            self._log.debug("Single frame captured", service_id=service_id)

            return frame_data

        except Exception as e:
            self._log.error(
                "Failed to capture frame",
                service_id=service_id,
                error=str(e),
            )
            return None

    def is_streaming(self, stream_id: str) -> bool:
        """Check if stream is currently active."""
        return stream_id in self._active_streams

    def get_active_streams(self) -> list[str]:
        """Get list of active stream IDs."""
        return list(self._active_streams.keys())
