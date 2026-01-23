"""
Execution engine for Web2API.

This module contains the core execution components:
- ChatExecutor: Web chat interface interaction
- OperationRunner: Operation execution engine
- ExecutionQueue: FIFO queue manager for operations
"""

from web2api.execution.chat_executor import (
    ChatExecutor,
    ChatConfig,
    ChatUISelectors,
    ChatExecutionResult,
    ChatUIDetector,
)
from web2api.execution.operation_runner import OperationRunner
from web2api.execution.queue_manager import ExecutionQueue

__all__ = [
    "ChatExecutor",
    "ChatConfig",
    "ChatUISelectors",
    "ChatExecutionResult",
    "ChatUIDetector",
    "OperationRunner",
    "ExecutionQueue",
]
