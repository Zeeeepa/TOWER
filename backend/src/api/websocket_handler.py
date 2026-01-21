"""
WebSocket handler for real-time updates to TOWER frontend.

Provides live streaming of discovery progress and execution logs.
"""

from __future__ import annotations

import time
from typing import Any

import structlog
from fastapi import WebSocket, WebSocketDisconnect

logger = structlog.get_logger(__name__)


class WebSocketHandler:
    """
    Manages WebSocket connections for real-time updates.

    Features:
    - Service-specific connections
    - Discovery progress updates
    - Execution log streaming
    - Connection lifecycle management
    """

    def __init__(self) -> None:
        """Initialize WebSocket handler."""
        self._active_connections: dict[str, WebSocket] = {}
        self._log = logger.bind(component="websocket_handler")

    async def connect(self, service_id: str, websocket: WebSocket) -> None:
        """
        Accept and register WebSocket connection.

        Args:
            service_id: Service identifier
            websocket: WebSocket connection
        """
        await websocket.accept()
        self._active_connections[service_id] = websocket

        self._log.info("WebSocket connected", service_id=service_id)

    async def disconnect(self, service_id: str) -> None:
        """
        Remove WebSocket connection.

        Args:
            service_id: Service identifier
        """
        if service_id in self._active_connections:
            del self._active_connections[service_id]

            self._log.info("WebSocket disconnected", service_id=service_id)

    async def send_discovery_update(
        self,
        service_id: str,
        status: str,
        progress: int,
        message: str,
    ) -> None:
        """
        Send discovery progress update.

        Args:
            service_id: Service identifier
            status: Discovery status (pending, scanning, complete, failed)
            progress: Progress percentage (0-100)
            message: Status message
        """
        if service_id not in self._active_connections:
            return

        try:
            await self._active_connections[service_id].send_json(
                {
                    "type": "discovery_update",
                    "timestamp": int(time.time() * 1000),
                    "service_id": service_id,
                    "discoveryStatus": status,
                    "progress": progress,
                    "message": message,
                }
            )

            self._log.debug(
                "Discovery update sent",
                service_id=service_id,
                status=status,
                progress=progress,
            )

        except Exception as e:
            self._log.error(
                "Failed to send discovery update",
                service_id=service_id,
                error=str(e),
            )

    async def send_execution_log(
        self,
        service_id: str | None,
        level: str,
        message: str,
    ) -> None:
        """
        Send execution log message.

        Args:
            service_id: Service identifier (None for broadcast)
            level: Log level (info, warning, error)
            message: Log message
        """
        # Broadcast to all if service_id is None
        if service_id is None:
            for ws_service_id, websocket in self._active_connections.items():
                try:
                    await websocket.send_json(
                        {
                            "type": "execution_log",
                            "timestamp": int(time.time() * 1000),
                            "service_id": ws_service_id,
                            "level": level,
                            "message": message,
                        }
                    )
                except Exception as e:
                    self._log.error(
                        "Failed to send execution log",
                        service_id=ws_service_id,
                        error=str(e),
                    )
            return

        # Send to specific service
        if service_id not in self._active_connections:
            return

        try:
            await self._active_connections[service_id].send_json(
                {
                    "type": "execution_log",
                    "timestamp": int(time.time() * 1000),
                    "service_id": service_id,
                    "level": level,
                    "message": message,
                }
            )

            self._log.debug(
                "Execution log sent",
                service_id=service_id,
                level=level,
            )

        except Exception as e:
            self._log.error(
                "Failed to send execution log",
                service_id=service_id,
                error=str(e),
            )

    async def send_stream_frame(
        self,
        service_id: str,
        frame_data: str,
    ) -> None:
        """
        Send live viewport stream frame.

        Args:
            service_id: Service identifier
            frame_data: Base64-encoded frame data
        """
        if service_id not in self._active_connections:
            return

        try:
            await self._active_connections[service_id].send_json(
                {
                    "type": "stream_frame",
                    "timestamp": int(time.time() * 1000),
                    "service_id": service_id,
                    "frame": frame_data,
                }
            )

        except Exception as e:
            self._log.error(
                "Failed to send stream frame",
                service_id=service_id,
                error=str(e),
            )

    async def send_error(
        self,
        service_id: str,
        error: str,
        details: dict[str, Any] | None = None,
    ) -> None:
        """
        Send error message.

        Args:
            service_id: Service identifier
            error: Error message
            details: Additional error details
        """
        if service_id not in self._active_connections:
            return

        try:
            await self._active_connections[service_id].send_json(
                {
                    "type": "error",
                    "timestamp": int(time.time() * 1000),
                    "service_id": service_id,
                    "error": error,
                    "details": details or {},
                }
            )

            self._log.error("Error sent", service_id=service_id, error=error)

        except Exception as e:
            self._log.error(
                "Failed to send error message",
                service_id=service_id,
                error=str(e),
            )

    def is_connected(self, service_id: str) -> bool:
        """Check if service has active WebSocket connection."""
        return service_id in self._active_connections

    async def close_all(self) -> None:
        """Close all active WebSocket connections."""
        self._log.info("Closing all WebSocket connections")

        for service_id, websocket in self._active_connections.items():
            try:
                await websocket.close()
            except Exception as e:
                self._log.error(
                    "Failed to close WebSocket",
                    service_id=service_id,
                    error=str(e),
                )

        self._active_connections.clear()
