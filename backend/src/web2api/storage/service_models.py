"""
Service management models for Web2API.

Extends AutoQA database with service, credential, and execution tracking.
"""

from __future__ import annotations

import uuid
from datetime import UTC, datetime

from sqlalchemy import JSON, Column, DateTime, Integer, String, Text, ForeignKey
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import relationship

from web2api.storage.base import Base


class ServiceModel(Base):
    """Web2API service database model."""

    __tablename__ = "services"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    name = Column(String(255), nullable=False)
    url = Column(Text, nullable=False)
    type = Column(String(50), default="generic")  # zai, chatgpt, claude, k2think, generic
    status = Column(String(50), default="offline")  # online, offline, maintenance, analyzing
    login_status = Column(String(50), default="pending")  # pending, processing, success, failed
    discovery_status = Column(String(50), default="pending")  # pending, scanning, complete
    config = Column(JSON, nullable=True)  # Service configuration JSON
    created_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))
    updated_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC), onupdate=lambda: datetime.now(UTC))

    # Relationships
    credentials = relationship("ServiceCredentialModel", back_populates="service", uselist=False, cascade="all, delete-orphan")
    sessions = relationship("ServiceSessionModel", back_populates="service", cascade="all, delete-orphan")
    endpoints = relationship("ServiceEndpointModel", back_populates="service", cascade="all, delete-orphan")
    executions = relationship("ServiceExecutionModel", back_populates="service", cascade="all, delete-orphan")
    stats = relationship("ServiceStatsModel", back_populates="service", uselist=False, cascade="all, delete-orphan")


class ServiceCredentialModel(Base):
    """Encrypted service credentials storage."""

    __tablename__ = "service_credentials"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    service_id = Column(UUID(as_uuid=True), ForeignKey("services.id", ondelete="CASCADE"), nullable=False, index=True)
    auth_type = Column(String(50), nullable=False)  # form_login, oauth, api_key
    encrypted_email = Column(Text, nullable=True)
    encrypted_password = Column(Text, nullable=True)
    encrypted_api_key = Column(Text, nullable=True)
    session_cookie = Column(JSON, nullable=True)  # Stored cookies for session persistence
    created_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))
    updated_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC), onupdate=lambda: datetime.now(UTC))

    # Relationship
    service = relationship("ServiceModel", back_populates="credentials")


class ServiceSessionModel(Base):
    """Browser session tracking for services."""

    __tablename__ = "service_sessions"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    service_id = Column(UUID(as_uuid=True), ForeignKey("services.id", ondelete="CASCADE"), nullable=False, index=True)
    owl_session_id = Column(String(255), nullable=True)  # Owl-Browser session/context ID
    tab_id = Column(String(255), nullable=True)  # Tab identifier for multi-service isolation
    cookies_ref = Column(JSON, nullable=True)  # Reference to stored cookies
    stream_id = Column(String(255), nullable=True)  # Live viewport stream ID
    recording_ref = Column(Text, nullable=True)  # Video recording path
    state = Column(JSON, nullable=True)  # Session state (e.g., login_status, current_url)
    status = Column(String(50), default="active")  # active, expired, terminated
    expires_at = Column(DateTime(timezone=True), nullable=True)
    created_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))
    last_used_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))

    # Relationship
    service = relationship("ServiceModel", back_populates="sessions")


class ServiceEndpointModel(Base):
    """Discovered service endpoints/operations."""

    __tablename__ = "service_endpoints"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    service_id = Column(UUID(as_uuid=True), ForeignKey("services.id", ondelete="CASCADE"), nullable=False, index=True)
    name = Column(String(255), nullable=False)  # e.g., "chat_completion", "image_generation"
    operation_type = Column(String(100), nullable=False)  # chat, completion, embedding, image_generation
    input_schema = Column(JSON, nullable=False)  # JSON Schema for inputs
    output_schema = Column(JSON, nullable=True)  # JSON Schema for outputs
    execution_steps = Column(JSON, nullable=False)  # Step-by-step execution plan
    ui_selectors = Column(JSON, nullable=True)  # Natural language selectors for UI elements
    version = Column(String(50), default="1.0")
    status = Column(String(50), default="active")  # active, deprecated, experimental
    created_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))
    updated_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC), onupdate=lambda: datetime.now(UTC))

    # Relationship
    service = relationship("ServiceModel", back_populates="endpoints")


class ServiceExecutionModel(Base):
    """Track service endpoint executions."""

    __tablename__ = "service_executions"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    service_id = Column(UUID(as_uuid=True), ForeignKey("services.id", ondelete="CASCADE"), nullable=False, index=True)
    endpoint_id = Column(UUID(as_uuid=True), ForeignKey("service_endpoints.id", ondelete="SET NULL"), nullable=True)
    operation_id = Column(String(100), nullable=False)  # e.g., "chat_completion"
    parameters = Column(JSON, nullable=False)  # Input parameters
    result = Column(Text, nullable=True)  # Execution result (text or JSON)
    status = Column(String(50), nullable=False, default="pending")  # pending, running, success, failed
    started_at = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))
    completed_at = Column(DateTime(timezone=True), nullable=True)
    duration_ms = Column(Integer, nullable=True)
    error = Column(Text, nullable=True)
    execution_metadata = Column(JSON, nullable=True)  # Additional execution metadata

    # Relationship
    service = relationship("ServiceModel", back_populates="executions")


class ServiceStatsModel(Base):
    """Service usage statistics."""

    __tablename__ = "service_stats"

    service_id = Column(UUID(as_uuid=True), ForeignKey("services.id", ondelete="CASCADE"), primary_key=True)
    total_requests = Column(Integer, nullable=False, default=0)
    successful_requests = Column(Integer, nullable=False, default=0)
    failed_requests = Column(Integer, nullable=False, default=0)
    avg_latency_ms = Column(Integer, nullable=False, default=0)
    last_request_at = Column(DateTime(timezone=True), nullable=True)
    uptime_start = Column(DateTime(timezone=True), nullable=False, default=lambda: datetime.now(UTC))

    # Relationship
    service = relationship("ServiceModel", back_populates="stats")
