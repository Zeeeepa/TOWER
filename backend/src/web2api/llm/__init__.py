"""
LLM Integration module for AutoQA.

Provides optional LLM enhancement for AutoQA tools:
- Test Builder: Generate meaningful test/step names and descriptions
- Step Transformer: Natural language to action translation
- Assertions: LLM-based semantic validation
- Self-Healing: Enhanced selector recovery

All features gracefully fall back when LLM is disabled.
"""

from web2api.llm.assertions import (
    LLMAssertionEngine,
    LLMAssertionError,
    LLMAssertionResult,
)
from web2api.llm.client import (
    ChatCompletion,
    ChatMessage,
    LLMAPIError,
    LLMAuthenticationError,
    LLMClient,
    LLMClientError,
    LLMClientFactory,
    LLMRateLimitError,
    create_llm_client,
)
from web2api.llm.config import (
    LLMConfig,
    LLMEndpointConfig,
    LLMProvider,
    LLMSettings,
    RateLimitConfig,
    RetryConfig,
    ToolLLMConfig,
    ToolName,
    load_llm_config,
)
from web2api.llm.prompts import (
    PromptTemplate,
    PromptType,
    format_prompt,
    get_prompt,
    get_prompt_config,
)
from web2api.llm.service import (
    LLMResult,
    LLMService,
    LLMServiceError,
    get_llm_service,
    shutdown_llm_service,
)

__all__ = [
    # Config
    "LLMConfig",
    "LLMEndpointConfig",
    "LLMProvider",
    "LLMSettings",
    "RateLimitConfig",
    "RetryConfig",
    "ToolLLMConfig",
    "ToolName",
    "load_llm_config",
    # Client
    "ChatCompletion",
    "ChatMessage",
    "LLMAPIError",
    "LLMAuthenticationError",
    "LLMClient",
    "LLMClientError",
    "LLMClientFactory",
    "LLMRateLimitError",
    "create_llm_client",
    # Prompts
    "PromptTemplate",
    "PromptType",
    "format_prompt",
    "get_prompt",
    "get_prompt_config",
    # Service
    "LLMResult",
    "LLMService",
    "LLMServiceError",
    "get_llm_service",
    "shutdown_llm_service",
    # Assertions
    "LLMAssertionEngine",
    "LLMAssertionError",
    "LLMAssertionResult",
]
