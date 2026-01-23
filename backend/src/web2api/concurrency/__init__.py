"""
Concurrency module for parallel test execution.

Provides:
- BrowserPool for efficient browser context reuse
- AsyncTestRunner for async test execution with concurrency control
- ResourceMonitor for memory-aware scaling
- Configuration models for parallel execution
"""

from web2api.concurrency.browser_pool import (
    BrowserContext,
    BrowserPool,
    BrowserPoolError,
    ContextAcquisitionError,
    PoolExhaustedError,
)
from web2api.concurrency.config import (
    ConcurrencyConfig,
    ResourceLimits,
    load_concurrency_config,
)
from web2api.concurrency.resource_monitor import (
    MemoryPressure,
    ResourceMonitor,
    ResourceSnapshot,
)
from web2api.concurrency.runner import (
    AsyncTestRunner,
    ParallelExecutionResult,
    TestExecutionContext,
)

__all__ = [
    # Browser Pool
    "BrowserContext",
    "BrowserPool",
    "BrowserPoolError",
    "ContextAcquisitionError",
    "PoolExhaustedError",
    # Configuration
    "ConcurrencyConfig",
    "ResourceLimits",
    "load_concurrency_config",
    # Resource Monitor
    "MemoryPressure",
    "ResourceMonitor",
    "ResourceSnapshot",
    # Async Runner
    "AsyncTestRunner",
    "ParallelExecutionResult",
    "TestExecutionContext",
]
