"""
FIFO queue manager for service operation execution.

Implements sequential per-service execution queue (no rate limiting).
"""

from __future__ import annotations

import asyncio
import time
import uuid
from typing import TYPE_CHECKING, Any

import structlog

from web2api.execution.operation_runner import OperationRunner

if TYPE_CHECKING:
    from owl_browser import Browser

logger = structlog.get_logger(__name__)


class ExecutionQueue:
    """
    FIFO queue for service execution requests.

    Features:
    - Per-service FIFO queues
    - Sequential processing per service (no rate limiting)
    - Async task management
    - WebSocket progress updates
    """

    def __init__(self) -> None:
        """Initialize execution queue manager."""
        self._queues: dict[str, asyncio.Queue] = {}
        self._processors: dict[str, asyncio.Task] = {}
        self._running_tasks: dict[str, Any] = {}
        self._lock = asyncio.Lock()
        self._log = logger.bind(component="execution_queue")

    async def add_task(
        self,
        service_id: str,
        service_config: dict[str, Any],
        operation_id: str,
        parameters: dict[str, Any],
        browser: Browser,
        context_id: str,
        websocket_handler: Any = None,
    ) -> str:
        """
        Add task to service queue.

        Args:
            service_id: Service identifier
            service_config: Service configuration
            operation_id: Operation to execute
            parameters: Operation parameters
            browser: Owl-Browser instance
            context_id: Browser context ID
            websocket_handler: Optional WebSocket for updates

        Returns:
            Task ID for tracking
        """
        async with self._lock:
            # Create queue for service if doesn't exist
            if service_id not in self._queues:
                self._queues[service_id] = asyncio.Queue()

                # Start processor task for this service
                self._processors[service_id] = asyncio.create_task(
                    self._process_queue(service_id, browser, websocket_handler)
                )

                self._log.info("Queue created for service", service_id=service_id)

        # Create task
        task = {
            "task_id": str(uuid.uuid4()),
            "service_id": service_id,
            "service_config": service_config,
            "operation_id": operation_id,
            "parameters": parameters,
            "context_id": context_id,
            "created_at": time.time(),
            "status": "pending",
        }

        # Add to queue
        await self._queues[service_id].put(task)

        # Track task
        self._running_tasks[task["task_id"]] = task

        self._log.info(
            "Task added to queue",
            task_id=task["task_id"],
            service_id=service_id,
            operation_id=operation_id,
            queue_size=self._queues[service_id].qsize(),
        )

        return task["task_id"]

    async def get_task_status(self, task_id: str) -> dict[str, Any] | None:
        """Get status of a task."""
        return self._running_tasks.get(task_id)

    async def _process_queue(
        self,
        service_id: str,
        browser: Browser,
        websocket_handler: Any = None,
    ) -> None:
        """
        Process tasks from service queue (sequential execution).

        Args:
            service_id: Service identifier
            browser: Owl-Browser instance
            websocket_handler: Optional WebSocket handler
        """
        self._log.info("Starting queue processor", service_id=service_id)

        runner = OperationRunner()

        while True:
            try:
                # Get next task from queue
                task = await self._queues[service_id].get()

                if task is None:
                    # Poison pill - stop processor
                    self._log.info("Stopping queue processor", service_id=service_id)
                    break

                task_id = task["task_id"]
                operation_id = task["operation_id"]

                self._log.info(
                    "Processing task",
                    task_id=task_id,
                    service_id=service_id,
                    operation_id=operation_id,
                )

                # Update task status
                task["status"] = "running"
                task["started_at"] = time.time()

                # Send WebSocket update
                if websocket_handler:
                    await websocket_handler.send_execution_log(
                        service_id,
                        "info",
                        f"Starting execution: {operation_id}",
                    )

                try:
                    # Execute operation
                    result = await runner.execute_operation(
                        service_id=task["service_id"],
                        service_config=task["service_config"],
                        operation_id=task["operation_id"],
                        parameters=task["parameters"],
                        browser=browser,
                        context_id=task["context_id"],
                        websocket_handler=websocket_handler,
                    )

                    # Task completed successfully
                    task["status"] = "completed"
                    task["result"] = result
                    task["completed_at"] = time.time()
                    task["duration_ms"] = int(
                        (task["completed_at"] - task["started_at"]) * 1000
                    )

                    self._log.info(
                        "Task completed",
                        task_id=task_id,
                        duration_ms=task["duration_ms"],
                    )

                    # Send WebSocket update
                    if websocket_handler:
                        await websocket_handler.send_execution_log(
                            service_id,
                            "info",
                            f"Execution completed: {operation_id}",
                        )

                except Exception as e:
                    # Task failed
                    task["status"] = "failed"
                    task["error"] = str(e)
                    task["completed_at"] = time.time()

                    self._log.error(
                        "Task execution failed",
                        task_id=task_id,
                        error=str(e),
                    )

                    # Send WebSocket update
                    if websocket_handler:
                        await websocket_handler.send_execution_log(
                            service_id,
                            "error",
                            f"Execution failed: {str(e)}",
                        )

                finally:
                    # Mark queue task as done
                    self._queues[service_id].task_done()

            except asyncio.CancelledError:
                self._log.info("Queue processor cancelled", service_id=service_id)
                break
            except Exception as e:
                self._log.error(
                    "Queue processor error",
                    service_id=service_id,
                    error=str(e),
                )

    async def stop_queue(self, service_id: str) -> None:
        """Stop queue processor for a service."""
        async with self._lock:
            if service_id in self._queues:
                # Send poison pill to stop processor
                await self._queues[service_id].put(None)

                # Wait for processor to finish
                if service_id in self._processors:
                    self._processors[service_id].cancel()
                    try:
                        await self._processors[service_id]
                    except asyncio.CancelledError:
                        pass

                    del self._processors[service_id]

                self._log.info("Queue stopped", service_id=service_id)

    def get_queue_size(self, service_id: str) -> int:
        """Get current queue size for service."""
        if service_id in self._queues:
            return self._queues[service_id].qsize()
        return 0

    async def shutdown(self) -> None:
        """Shutdown all queue processors."""
        self._log.info("Shutting down all queue processors")

        for service_id in list(self._queues.keys()):
            await self.stop_queue(service_id)
