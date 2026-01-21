"""
Video recording for discovery and execution processes.

Records browser sessions for later playback and debugging.
"""

from __future__ import annotations

import os
import time
import uuid
from typing import TYPE_CHECKING, Any

import structlog

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class VideoRecorder:
    """
    Records browser sessions during discovery and execution.

    Uses Owl-Browser commands:
    - start_video_recording: Start recording
    - pause_video_recording: Pause recording
    - resume_video_recording: Resume recording
    - stop_video_recording: Stop recording
    - download_video_recording: Download video file
    """

    def __init__(self, storage_path: str = "./recordings") -> None:
        """
        Initialize video recorder.

        Args:
            storage_path: Path to save video recordings
        """
        self._storage_path = storage_path
        self._active_recordings: dict[str, dict[str, Any]] = {}
        self._log = logger.bind(component="video_recorder")

    async def start_recording(
        self,
        service_id: str,
        browser: Browser,
        context_id: str,
    ) -> str:
        """
        Start video recording for a service session.

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            context_id: Browser context ID

        Returns:
            Recording ID
        """
        try:
            # Start video recording using Owl-Browser
            recording_result = await browser.start_video_recording({
                "context_id": context_id,
                "quality": "medium",
            })

            recording_id = recording_result.get("recording_id", f"rec_{service_id}_{uuid.uuid4().hex[:8]}")

            # Store recording info
            self._active_recordings[recording_id] = {
                "service_id": service_id,
                "context_id": context_id,
                "started_at": time.time(),
                "status": "recording",
            }

            self._log.info(
                "Video recording started",
                recording_id=recording_id,
                service_id=service_id,
            )

            return recording_id

        except Exception as e:
            self._log.error(
                "Failed to start recording",
                service_id=service_id,
                error=str(e),
            )
            raise

    async def pause_recording(
        self,
        recording_id: str,
        browser: Browser,
    ) -> None:
        """
        Pause active recording.

        Args:
            recording_id: Recording identifier
            browser: Owl-Browser instance
        """
        try:
            if recording_id not in self._active_recordings:
                self._log.warning("Recording not found", recording_id=recording_id)
                return

            # Pause recording using Owl-Browser
            await browser.pause_video_recording({"recording_id": recording_id})

            self._active_recordings[recording_id]["status"] = "paused"

            self._log.info("Recording paused", recording_id=recording_id)

        except Exception as e:
            self._log.error(
                "Failed to pause recording",
                recording_id=recording_id,
                error=str(e),
            )

    async def resume_recording(
        self,
        recording_id: str,
        browser: Browser,
    ) -> None:
        """
        Resume paused recording.

        Args:
            recording_id: Recording identifier
            browser: Owl-Browser instance
        """
        try:
            if recording_id not in self._active_recordings:
                self._log.warning("Recording not found", recording_id=recording_id)
                return

            # Resume recording using Owl-Browser
            await browser.resume_video_recording({"recording_id": recording_id})

            self._active_recordings[recording_id]["status"] = "recording"

            self._log.info("Recording resumed", recording_id=recording_id)

        except Exception as e:
            self._log.error(
                "Failed to resume recording",
                recording_id=recording_id,
                error=str(e),
            )

    async def stop_and_save(
        self,
        recording_id: str,
        browser: Browser,
        service_id: str,
    ) -> str | None:
        """
        Stop recording and save to storage.

        Args:
            recording_id: Recording identifier
            browser: Owl-Browser instance
            service_id: Service identifier

        Returns:
            Path to saved video file
        """
        try:
            if recording_id not in self._active_recordings:
                self._log.warning("Recording not found", recording_id=recording_id)
                return None

            # Stop recording using Owl-Browser
            await browser.stop_video_recording({"recording_id": recording_id})

            # Download video
            download_result = await browser.download_video_recording({
                "recording_id": recording_id,
            })

            video_data = download_result.get("data")

            if not video_data:
                self._log.error("No video data returned", recording_id=recording_id)
                return None

            # Save to storage
            os.makedirs(self._storage_path, exist_ok=True)

            # Sanitize service_id to prevent path traversal
            safe_service_id = "".join(c if c.isalnum() or c in ('-', '_') else '_' for c in service_id)
            filename = f"{safe_service_id}_{recording_id}_{int(time.time())}.mp4"
            filepath = os.path.join(self._storage_path, filename)

            # Decode and save (assuming base64)
            import base64

            with open(filepath, "wb") as f:
                f.write(base64.b64decode(video_data))

            # Update recording info
            self._active_recordings[recording_id]["status"] = "completed"
            self._active_recordings[recording_id]["filepath"] = filepath
            self._active_recordings[recording_id]["completed_at"] = time.time()

            self._log.info(
                "Recording saved",
                recording_id=recording_id,
                filepath=filepath,
            )

            # Remove from active recordings
            del self._active_recordings[recording_id]

            return filepath

        except Exception as e:
            self._log.error(
                "Failed to save recording",
                recording_id=recording_id,
                error=str(e),
            )
            return None

    def get_recording_status(self, recording_id: str) -> dict[str, Any] | None:
        """Get status of a recording."""
        return self._active_recordings.get(recording_id)

    def get_active_recordings(self) -> list[str]:
        """Get list of active recording IDs."""
        return [
            rec_id
            for rec_id, info in self._active_recordings.items()
            if info["status"] == "recording"
        ]
