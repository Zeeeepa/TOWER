"""
OpenAI-compatible API endpoints for Web2API.

Converts OpenAI-format requests to Web2API service executions.
This is the main entry point for using web services as OpenAI-compatible APIs.

Workflow:
1. User sends OpenAI-format request with service_id as "model"
2. System loads service configuration and credentials
3. Browser session is restored (or login is performed if first time)
4. Message is sent to the web chat interface
5. Response is extracted and formatted as OpenAI response
"""

from __future__ import annotations

import asyncio
import time
import uuid
from typing import Any, AsyncIterator

import structlog
from fastapi import APIRouter, HTTPException, status
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field

logger = structlog.get_logger(__name__)

router = APIRouter()


# OpenAI API Models
class ChatMessage(BaseModel):
    """Chat message."""

    role: str
    content: str


class ChatCompletionRequest(BaseModel):
    """OpenAI chat completion request."""

    model: str  # This will be the service_id
    messages: list[ChatMessage]
    temperature: float | None = None
    max_tokens: int | None = None
    stream: bool = False


class ChatCompletionChoice(BaseModel):
    """Chat completion choice."""

    index: int
    message: ChatMessage
    finish_reason: str


class ChatCompletionUsage(BaseModel):
    """Token usage information."""

    prompt_tokens: int
    completion_tokens: int
    total_tokens: int


class ChatCompletionResponse(BaseModel):
    """OpenAI chat completion response."""

    id: str
    object: str = "chat.completion"
    created: int
    model: str
    choices: list[ChatCompletionChoice]
    usage: ChatCompletionUsage


class ModelInfo(BaseModel):
    """Model information."""

    id: str
    object: str = "model"
    created: int | None = None
    owned_by: str = "web2api"


class ModelsResponse(BaseModel):
    """List of available models."""

    object: str = "list"
    data: list[ModelInfo]


# Dependencies (will be injected by main app)
class ServiceContainer:
    """Container for service dependencies."""

    def __init__(self):
        self.db = None
        self.browser = None
        self.queue_manager = None
        self.websocket_handler = None


_container = ServiceContainer()


def set_container(db, browser, queue_manager, websocket_handler=None):
    """Set service container dependencies."""
    _container.db = db
    _container.browser = browser
    _container.queue_manager = queue_manager
    _container.websocket_handler = websocket_handler


@router.post("/v1/chat/completions", response_model=ChatCompletionResponse)
async def chat_completion(request: ChatCompletionRequest) -> ChatCompletionResponse:
    """
    OpenAI-compatible chat completion endpoint.

    This is the main Web2API endpoint that:
    1. Receives OpenAI-format request
    2. Loads service configuration (URL + credentials)
    3. Opens headless browser and navigates to service
    4. Restores cookies from past sessions (if available)
    5. If first time/session expired: performs login and saves cookies
    6. Types the message into the chat input field
    7. Clicks send and tracks button state for completion
    8. Extracts response and formats as OpenAI response
    """
    log = logger.bind(component="chat_completion", model=request.model)

    try:
        # Validate dependencies
        if not _container.db or not _container.browser or not _container.queue_manager:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="Service not ready. Please ensure browser and database are connected.",
            )

        # Get service from database
        from sqlalchemy import select

        # Validate and parse service ID as UUID
        try:
            service_uuid = uuid.UUID(request.model)
        except ValueError:
            log.warning("Invalid service ID format", service_id=request.model)
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Invalid service ID format. Service ID must be a valid UUID.",
            )

        result = await _container.db.execute(
            select(ServiceModel).where(ServiceModel.id == service_uuid)
        )
        service = result.scalar_one_or_none()

        if not service:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Service {request.model} not found",
            )

        log.info("Processing chat completion", service_id=str(service.id), url=service.url)

        # Get last message from user
        user_message = ""
        for msg in reversed(request.messages):
            if msg.role == "user":
                user_message = msg.content
                break

        if not user_message:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="No user message provided",
            )

        # Get service configuration (should be saved from discovery)
        service_config = service.config or {}

        # Auto-configure if not discovered yet
        if not service_config or not service_config.get("ui_selectors"):
            log.info("Service not configured, running auto-discovery...")
            service_config = await _auto_discover_service(
                service_id=str(service.id),
                url=service.url,
                browser=_container.browser,
                context_id="default",
            )
            
            # Save config to database
            service.config = service_config
            await _container.db.commit()

        # Get credentials
        from web2api.auth.credential_store import CredentialStore

        cred_store = CredentialStore()
        cred_result = await _container.db.execute(
            select(ServiceCredentialModel).where(
                ServiceCredentialModel.service_id == service.id
            )
        )
        credentials_db = cred_result.scalar_one_or_none()

        if not credentials_db:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Service credentials not found. Please add credentials first.",
            )

        # Decrypt credentials
        credentials = cred_store.decrypt_credentials(
            encrypted_email=credentials_db.encrypted_email,
            encrypted_password=credentials_db.encrypted_password,
        )

        # Ensure authenticated session
        from web2api.auth.session_manager import SessionManager
        from web2api.auth.form_filler import FormFiller
        from web2api.storage.database import SessionCookieStorage

        session_manager = SessionManager()
        cookie_storage = SessionCookieStorage(_container.db)
        context_id = f"service_{service.id}"  # Use service-specific context

        # Try to restore existing session (loads cookies)
        session_restored = await session_manager.restore_session(
            str(service.id),
            _container.browser,
            context_id=context_id,
            storage=cookie_storage,
            service_url=service.url,
        )

        # If no session or session invalid, need to login
        if not session_restored:
            log.info("No valid session, performing login...")
            
            # Navigate to service URL first
            await _container.browser.browser_navigate({
                "context_id": context_id,
                "url": service.url,
            })
            await _container.browser.browser_wait_for_load({
                "context_id": context_id,
                "state": "domcontentloaded",
                "timeout": 30000,
            })
            
            # Perform login
            form_filler = FormFiller()
            login_success = await form_filler.complete_login_flow(
                _container.browser,
                context_id,
                credentials,
            )

            if not login_success:
                raise HTTPException(
                    status_code=status.HTTP_401_UNAUTHORIZED,
                    detail="Failed to authenticate with service. Check credentials.",
                )

            # Save session cookies for future requests
            await session_manager.save_session(
                str(service.id),
                _container.browser,
                context_id,
                cookie_storage,
                service_url=service.url,
            )
            
            log.info("Login successful, session saved")
        else:
            log.info("Session restored from cookies")

        # Execute chat completion operation
        task_id = await _container.queue_manager.add_task(
            service_id=str(service.id),
            service_config=service_config,
            operation_id="chat_completion",
            parameters={"message": user_message},
            browser=_container.browser,
            context_id=context_id,
            websocket_handler=_container.websocket_handler,
        )

        # Wait for task to complete
        max_wait = 180  # 3 minutes max wait
        waited = 0
        while waited < max_wait:
            task = await _container.queue_manager.get_task_status(task_id)
            if task and task["status"] in ["completed", "failed"]:
                break
            await asyncio.sleep(0.5)
            waited += 0.5

        if not task or task["status"] == "failed":
            error_msg = task.get("error", "Unknown error") if task else "Task not found"
            raise HTTPException(
                status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                detail=f"Execution failed: {error_msg}",
            )

        # Get result
        result = task.get("result", "")

        # Estimate token counts (rough approximation)
        prompt_tokens = len(user_message.split()) * 1.3  # Rough estimate
        completion_tokens = len(result.split()) * 1.3
        total_tokens = int(prompt_tokens + completion_tokens)

        # Create OpenAI-format response
        response = ChatCompletionResponse(
            id=f"chatcmpl-{uuid.uuid4().hex[:24]}",
            created=int(time.time()),
            model=request.model,
            choices=[
                ChatCompletionChoice(
                    index=0,
                    message=ChatMessage(role="assistant", content=result),
                    finish_reason="stop",
                )
            ],
            usage=ChatCompletionUsage(
                prompt_tokens=int(prompt_tokens),
                completion_tokens=int(completion_tokens),
                total_tokens=total_tokens,
            ),
        )

        log.info(
            "Chat completion successful",
            tokens=total_tokens,
            response_length=len(result),
        )
        
        # Handle streaming response
        if request.stream:
            completion_id = f"chatcmpl-{uuid.uuid4().hex[:24]}"
            return StreamingResponse(
                generate_stream_response(completion_id, request.model, result),
                media_type="text/event-stream",
                headers={
                    "Cache-Control": "no-cache",
                    "Connection": "keep-alive",
                },
            )

        return response

    except HTTPException:
        raise
    except Exception as e:
        log.error("Chat completion failed", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Chat completion failed: {str(e)}",
        )


async def _auto_discover_service(
    service_id: str,
    url: str,
    browser,
    context_id: str,
) -> dict[str, Any]:
    """
    Auto-discover chat UI elements for a service.
    
    Returns minimal config needed for chat operation.
    """
    from web2api.execution.chat_executor import ChatUIDetector
    
    # Navigate to service
    await browser.browser_navigate({
        "context_id": context_id,
        "url": url,
    })
    await browser.browser_wait_for_load({
        "context_id": context_id,
        "state": "domcontentloaded",
        "timeout": 30000,
    })
    
    # Detect chat UI elements
    detector = ChatUIDetector()
    selectors = await detector.detect_chat_ui(browser, context_id)
    
    if not selectors:
        raise ValueError(
            "Could not auto-detect chat UI elements. "
            "Please manually configure the service selectors."
        )
    
    return {
        "service_id": service_id,
        "url": url,
        "type": "chat",
        "ui_selectors": {
            "input": selectors.input_selector,
            "submit": selectors.send_button_selector,
            "output": selectors.output_selector,
            "stop_button": selectors.stop_button,
            "new_chat_button": selectors.new_chat_button,
        },
        "operations": [
            {
                "id": "chat_completion",
                "name": "Chat Completion",
                "type": "chat",
            }
        ],
    }


@router.get("/v1/models", response_model=ModelsResponse)
async def list_models() -> ModelsResponse:
    """
    List available models (services).

    Returns all registered Web2API services as "models".
    """
    log = logger.bind(component="list_models")

    try:
        if not _container.db:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="Service not ready",
            )

        from sqlalchemy import select

        result = await _container.db.execute(select(ServiceModel))
        services = result.scalars().all()

        models = [
            ModelInfo(
                id=str(service.id),
                created=int(service.created_at.timestamp()),
                owned_by="web2api",
            )
            for service in services
        ]

        log.info("Models listed", count=len(models))

        return ModelsResponse(data=models)

    except HTTPException:
        raise
    except Exception as e:
        log.error("Failed to list models", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to list models. Please try again later.",
        )


# ============================================================================
# Streaming Support (SSE)
# ============================================================================

class ChatCompletionChunkChoice(BaseModel):
    """Streaming chunk choice."""
    
    index: int
    delta: dict[str, str]
    finish_reason: str | None = None


class ChatCompletionChunk(BaseModel):
    """Streaming chunk response."""
    
    id: str
    object: str = "chat.completion.chunk"
    created: int
    model: str
    choices: list[ChatCompletionChunkChoice]


async def generate_stream_response(
    completion_id: str,
    model: str,
    result: str,
) -> AsyncIterator[str]:
    """
    Generate SSE stream from the result.
    
    Since web chat interfaces don't provide true streaming,
    we simulate streaming by yielding chunks of the final response.
    """
    created = int(time.time())
    
    # Send initial chunk with role
    initial_chunk = ChatCompletionChunk(
        id=completion_id,
        created=created,
        model=model,
        choices=[
            ChatCompletionChunkChoice(
                index=0,
                delta={"role": "assistant"},
            )
        ],
    )
    yield f"data: {initial_chunk.model_dump_json()}\n\n"
    
    # Split result into chunks and yield
    # Use word boundaries for natural chunking
    words = result.split(" ")
    chunk_size = 5  # Words per chunk
    
    for i in range(0, len(words), chunk_size):
        chunk_words = words[i:i + chunk_size]
        chunk_text = " ".join(chunk_words)
        
        # Add space before next chunk (except at start)
        if i > 0:
            chunk_text = " " + chunk_text
        
        chunk = ChatCompletionChunk(
            id=completion_id,
            created=created,
            model=model,
            choices=[
                ChatCompletionChunkChoice(
                    index=0,
                    delta={"content": chunk_text},
                )
            ],
        )
        yield f"data: {chunk.model_dump_json()}\n\n"
        
        # Small delay to simulate streaming
        await asyncio.sleep(0.02)
    
    # Send final chunk with finish_reason
    final_chunk = ChatCompletionChunk(
        id=completion_id,
        created=created,
        model=model,
        choices=[
            ChatCompletionChunkChoice(
                index=0,
                delta={},
                finish_reason="stop",
            )
        ],
    )
    yield f"data: {final_chunk.model_dump_json()}\n\n"
    yield "data: [DONE]\n\n"


# Import for type hints
from web2api.storage.service_models import ServiceModel, ServiceCredentialModel
import asyncio
