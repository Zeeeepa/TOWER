"""
Web2API - Universal API Gateway.

Transforms web-based AI chat interfaces into OpenAI-compatible API endpoints.

Features:
- Auto-discovery of web service capabilities
- OpenAI-compatible API endpoints (/v1/chat/completions, /v1/models)
- Session management with cookie persistence
- Self-healing UI interactions
- Visual inspection using vision models
- YAML-based flow configuration
"""

__version__ = "2.0.0"
__author__ = "Olib AI"

from web2api.assertions.engine import AssertionEngine
from web2api.builder.test_builder import (
    AutoTestBuilder,
    BuilderConfig,
    ElementInfo,
    PageAnalysis,
)
from web2api.concurrency import (
    AsyncTestRunner,
    BrowserPool,
    ConcurrencyConfig,
    MemoryPressure,
    ParallelExecutionResult,
    ResourceLimits,
    ResourceMonitor,
    load_concurrency_config,
)
from web2api.dsl.models import (
    AssertionConfig,
    ContentAssertionConfig,
    LLMAssertionConfig,
    NetworkAssertionConfig,
    SemanticAssertionConfig,
    TestSpec,
    TestStep,
    TestSuite,
    VersioningConfigModel,
    VisualAssertionConfig,
)
from web2api.dsl.parser import DSLParser
from web2api.generative.chaos_agents import ChaosAgentFactory, ChaosPersona
from web2api.orchestrator.scheduler import TestOrchestrator
from web2api.runner.self_healing import SelfHealingEngine
from web2api.runner.test_runner import TestRunner
from web2api.storage.artifact_manager import ArtifactManager
from web2api.versioning import (
    TestRunHistory,
    TestSnapshot,
    VersionDiff,
    VersionDiffAnalyzer,
    VersioningConfig,
)
from web2api.visual.regression_engine import VisualRegressionEngine

# LLM integration (optional)
from web2api.llm import (
    LLMAssertionEngine,
    LLMClient,
    LLMConfig,
    LLMService,
    ToolName as LLMToolName,
    get_llm_service,
    load_llm_config,
)

__all__ = [
    # Core
    "ArtifactManager",
    "AssertionConfig",
    "AssertionEngine",
    "AutoTestBuilder",
    "BuilderConfig",
    "ChaosAgentFactory",
    "ChaosPersona",
    "ContentAssertionConfig",
    "DSLParser",
    "ElementInfo",
    "LLMAssertionConfig",
    "NetworkAssertionConfig",
    "PageAnalysis",
    "SelfHealingEngine",
    "SemanticAssertionConfig",
    "TestOrchestrator",
    "TestRunHistory",
    "TestRunner",
    "TestSnapshot",
    "TestSpec",
    "TestStep",
    "TestSuite",
    "VersionDiff",
    "VersionDiffAnalyzer",
    "VersioningConfig",
    "VersioningConfigModel",
    "VisualAssertionConfig",
    "VisualRegressionEngine",
    "__version__",
    # Concurrency
    "AsyncTestRunner",
    "BrowserPool",
    "ConcurrencyConfig",
    "MemoryPressure",
    "ParallelExecutionResult",
    "ResourceLimits",
    "ResourceMonitor",
    "load_concurrency_config",
    # LLM integration
    "LLMAssertionEngine",
    "LLMClient",
    "LLMConfig",
    "LLMService",
    "LLMToolName",
    "get_llm_service",
    "load_llm_config",
]
