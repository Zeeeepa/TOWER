"""
OpenAI-compatible API endpoints for Web2API.

Converts OpenAI-format requests to Web2API service executions.
"""

from __future__ import annotations

import time
import uuid
from typing import Any

import structlog
from fastapi import APIRouter, HTTPException, status
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

    Maps OpenAI request to Web2API service execution.
    """
    log = logger.bind(component="chat_completion", model=request.model)

    try:
        # Validate dependencies
        if not _container.db or not _container.browser or not _container.queue_manager:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="Service not ready",
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

        log.info("Processing chat completion", service_id=str(service.id))

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

        # If not discovered yet, return error
        if not service_config:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Service not yet discovered. Please run discovery first.",
            )

        # Get credentials
        from autoqa.auth.credential_store import CredentialStore

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
                detail="Service credentials not found",
            )

        # Decrypt credentials
        credentials = cred_store.decrypt_credentials(
            encrypted_email=credentials_db.encrypted_email,
            encrypted_password=credentials_db.encrypted_password,
        )

        # Get or create session
        from autoqa.auth.session_manager import SessionManager

        session_manager = SessionManager()

        # Try to restore existing session
        session_restored = await session_manager.restore_session(
            str(service.id),
            _container.browser,
            context_id="default",  # Would be dynamic in production
            storage=_container.db,
        )

        # If no session, need to login
        if not session_restored:
            from autoqa.auth.form_filler import FormFiller

            form_filler = FormFiller()
            login_success = await form_filler.complete_login_flow(
                _container.browser,
                "default",
                credentials,
            )

            if not login_success:
                raise HTTPException(
                    status_code=status.HTTP_401_UNAUTHORIZED,
                    detail="Failed to authenticate with service",
                )

            # Save session
            await session_manager.save_session(
                str(service.id),
                _container.browser,
                "default",
                _container.db,
            )

        # Execute operation
        task_id = await _container.queue_manager.add_task(
            service_id=str(service.id),
            service_config=service_config,
            operation_id="chat_completion",
            parameters={"message": user_message},
            browser=_container.browser,
            context_id="default",
            websocket_handler=_container.websocket_handler,
        )

        # Wait for task to complete (simple polling for now)
        max_wait = 60  # 60 seconds
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

        log.info("Chat completion successful", tokens=total_tokens)

        return response

    except HTTPException:
        raise
    except Exception as e:
        log.error("Chat completion failed", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Chat completion failed. Please try again later.",
        )


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


# Import for type hints
from autoqa.storage.service_models import ServiceModel, ServiceCredentialModel
import asyncio
