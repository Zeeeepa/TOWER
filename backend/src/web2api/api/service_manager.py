"""
Service management endpoints for TOWER frontend integration.

Provides CRUD operations for services and triggers discovery.
"""

from __future__ import annotations

import uuid
from datetime import UTC, datetime
from typing import Any

import structlog
from fastapi import APIRouter, HTTPException, status, Depends
from pydantic import BaseModel, EmailStr, HttpUrl

from web2api.auth.credential_store import CredentialStore
from web2api.discovery.orchestrator import DiscoveryOrchestrator
from web2api.storage.service_models import (
    ServiceModel,
    ServiceCredentialModel,
    ServiceSessionModel,
)
from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession

logger = structlog.get_logger(__name__)

router = APIRouter(prefix="/api/services", tags=["services"])


# Request/Response Models
class RegisterServiceRequest(BaseModel):
    """Request to register a new service."""

    url: HttpUrl
    email: EmailStr
    password: str
    name: str | None = None
    type: str = "generic"


class ServiceResponse(BaseModel):
    """Service response."""

    id: str
    name: str
    url: str
    type: str
    status: str
    login_status: str
    discovery_status: str
    created_at: datetime
    updated_at: datetime


class DiscoveryStatus(BaseModel):
    """Discovery status response."""

    service_id: str
    status: str
    progress: int
    message: str


# Dependencies
async def get_db():
    """Get database session."""
    # This will be provided by the main app
    raise NotImplementedError


@router.post("/", response_model=ServiceResponse, status_code=status.HTTP_201_CREATED)
async def register_service(
    request: RegisterServiceRequest,
    db: AsyncSession = Depends(get_db),
) -> ServiceResponse:
    """
    Register a new service for Web2API.

    Args:
        request: Service registration details
        db: Database session

    Returns:
        Created service information
    """
    log = logger.bind(component="register_service")

    try:
        # Check if service already exists with this URL
        result = await db.execute(
            select(ServiceModel).where(ServiceModel.url == str(request.url))
        )
        existing = result.scalar_one_or_none()

        if existing:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=f"Service with URL {request.url} already registered",
            )

        # Generate service ID
        service_id = uuid.uuid4()

        # Encrypt credentials
        cred_store = CredentialStore()
        encrypted_creds = cred_store.encrypt_credentials(
            email=request.email,
            password=request.password,
        )

        # Create service
        service = ServiceModel(
            id=service_id,
            name=request.name or _extract_service_name(request.url),
            url=str(request.url),
            type=request.type,
            status="offline",
            login_status="pending",
            discovery_status="pending",
        )

        # Create credentials
        credentials = ServiceCredentialModel(
            service_id=service_id,
            auth_type="form_login",
            **encrypted_creds,
        )

        db.add(service)
        db.add(credentials)
        await db.commit()

        log.info(
            "Service registered",
            service_id=str(service_id),
            url=service.url,
            name=service.name,
        )

        return ServiceResponse(
            id=str(service.id),
            name=service.name,
            url=service.url,
            type=service.type,
            status=service.status,
            login_status=service.login_status,
            discovery_status=service.discovery_status,
            created_at=service.created_at,
            updated_at=service.updated_at,
        )

    except HTTPException:
        raise
    except Exception as e:
        log.error("Failed to register service", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Failed to register service: {str(e)}",
        )


@router.get("/", response_model=list[ServiceResponse])
async def list_services(
    db: AsyncSession = Depends(get_db),
) -> list[ServiceResponse]:
    """List all registered services."""
    try:
        result = await db.execute(select(ServiceModel).order_by(ServiceModel.created_at.desc()))
        services = result.scalars().all()

        return [
            ServiceResponse(
                id=str(service.id),
                name=service.name,
                url=service.url,
                type=service.type,
                status=service.status,
                login_status=service.login_status,
                discovery_status=service.discovery_status,
                created_at=service.created_at,
                updated_at=service.updated_at,
            )
            for service in services
        ]

    except Exception as e:
        logger.error("Failed to list services", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Failed to list services: {str(e)}",
        )


@router.get("/{service_id}", response_model=ServiceResponse)
async def get_service(
    service_id: str,
    db: AsyncSession = Depends(get_db),
) -> ServiceResponse:
    """Get service details by ID."""
    try:
        result = await db.execute(
            select(ServiceModel).where(ServiceModel.id == uuid.UUID(service_id))
        )
        service = result.scalar_one_or_none()

        if not service:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Service {service_id} not found",
            )

        return ServiceResponse(
            id=str(service.id),
            name=service.name,
            url=service.url,
            type=service.type,
            status=service.status,
            login_status=service.login_status,
            discovery_status=service.discovery_status,
            created_at=service.created_at,
            updated_at=service.updated_at,
        )

    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to get service", service_id=service_id, error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Failed to get service: {str(e)}",
        )


@router.delete("/{service_id}")
async def delete_service(
    service_id: str,
    db: AsyncSession = Depends(get_db),
) -> dict[str, str]:
    """Delete a service."""
    try:
        # Delete service (cascade will delete credentials, sessions, etc.)
        await db.execute(
            delete(ServiceModel).where(ServiceModel.id == uuid.UUID(service_id))
        )
        await db.commit()

        logger.info("Service deleted", service_id=service_id)

        return {"status": "deleted", "service_id": service_id}

    except Exception as e:
        logger.error("Failed to delete service", service_id=service_id, error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Failed to delete service: {str(e)}",
        )


def _extract_service_name(url: str) -> str:
    """Extract service name from URL."""
    try:
        from urllib.parse import urlparse

        parsed = urlparse(url)
        domain = parsed.netloc.replace("www.", "").split(".")[0]
        return domain.capitalize()
    except Exception:
        return "Unknown Service"
