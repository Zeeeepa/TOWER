"""
FastAPI application for AutoQA + Web2API.

Provides:
- Legacy AutoQA test management endpoints
- Web2API service management
- OpenAI-compatible API endpoints
- WebSocket support for real-time updates
"""

from __future__ import annotations

import os
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from datetime import UTC, datetime
from typing import Any

import structlog
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from owl_browser import Browser, RemoteConfig

# AutoQA imports
from autoqa.orchestrator.scheduler import TestOrchestrator
from autoqa.storage.artifact_manager import ArtifactManager
from autoqa.storage.database import DatabaseManager, TestResultRepository

# Web2API imports
from autoqa.execution.queue_manager import ExecutionQueue
from autoqa.api.websocket_handler import WebSocketHandler
from autoqa.api.openai_compat import set_container, router as openai_router
from autoqa.concurrency.browser_pool import BrowserPool
from autoqa.concurrency.config import ConcurrencyConfig

logger = structlog.get_logger(__name__)


class AppState:
    """Application state container."""

    # AutoQA components
    orchestrator: TestOrchestrator | None = None
    db_manager: DatabaseManager | None = None
    repository: TestResultRepository | None = None
    artifact_manager: ArtifactManager | None = None

    # Web2API components
    browser: Browser | None = None
    browser_pool: BrowserPool | None = None
    queue_manager: ExecutionQueue | None = None
    websocket_handler: WebSocketHandler | None = None


state = AppState()


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncGenerator[None, None]:
    """Application lifespan management."""
    log = logger.bind(component="api")
    log.info("Starting Web2API + AutoQA server")

    # Initialize AutoQA components
    state.orchestrator = TestOrchestrator(
        redis_url=os.environ.get("REDIS_URL"),
        max_concurrent_jobs=int(os.environ.get("MAX_CONCURRENT_JOBS", "10")),
    )
    await state.orchestrator.connect()

    database_url = os.environ.get("DATABASE_URL")
    if database_url:
        state.db_manager = DatabaseManager(database_url=database_url)
        await state.db_manager.create_tables()
        state.repository = TestResultRepository(state.db_manager)

    state.artifact_manager = ArtifactManager(
        storage_path=os.environ.get("ARTIFACT_PATH", "./artifacts"),
        s3_bucket=os.environ.get("S3_BUCKET"),
    )

    # Initialize Web2API components
    owl_browser_url = os.environ.get("OWL_BROWSER_URL")
    owl_browser_token = os.environ.get("OWL_BROWSER_TOKEN")

    if owl_browser_url:
        try:
            remote_config = RemoteConfig(url=owl_browser_url, token=owl_browser_token)
            state.browser = Browser(remote=remote_config)
            state.browser.launch()

            # Initialize browser pool
            config = ConcurrencyConfig(
                max_parallel_tests=int(os.environ.get("MAX_PARALLEL_TESTS", "5")),
                max_browser_contexts=int(os.environ.get("MAX_BROWSER_CONTEXTS", "10")),
            )
            state.browser_pool = BrowserPool(state.browser, config)
            await state.browser_pool.start()

            # Initialize queue manager
            state.queue_manager = ExecutionQueue()

            # Initialize WebSocket handler
            state.websocket_handler = WebSocketHandler()

            # Set container for OpenAI compatibility
            set_container(
                db=state.db_manager,
                browser=state.browser,
                queue_manager=state.queue_manager,
                websocket_handler=state.websocket_handler,
            )

            log.info("Web2API components initialized")

        except Exception as e:
            log.error("Failed to initialize Web2API components", error=str(e))

    log.info("Server started successfully")

    yield

    log.info("Shutting down server")

    # Cleanup Web2API components
    if state.queue_manager:
        await state.queue_manager.shutdown()

    if state.websocket_handler:
        await state.websocket_handler.close_all()

    if state.browser_pool:
        await state.browser_pool.stop()

    if state.browser:
        state.browser.close()

    # Cleanup AutoQA components
    if state.orchestrator:
        await state.orchestrator.disconnect()

    if state.db_manager:
        await state.db_manager.close()


def create_app() -> FastAPI:
    """Create FastAPI application."""
    app = FastAPI(
        title="Web2API + AutoQA",
        description="Universal API Gateway + AI Testing Platform",
        version="2.0.0",
        lifespan=lifespan,
    )

    app.add_middleware(
        CORSMiddleware,
        allow_origins=os.environ.get("CORS_ORIGINS", "*").split(","),
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    # Include Web2API routers
    try:
        app.include_router(openai_router)
    except Exception as e:
        logger.warning("Failed to include OpenAI router", error=str(e))

    @app.get("/health")
    async def health_check() -> dict[str, Any]:
        """Health check endpoint."""
        return {
            "status": "healthy",
            "timestamp": datetime.now(UTC).isoformat(),
            "version": "2.0.0",
            "web2api_ready": state.browser is not None,
            "autoqa_ready": state.orchestrator is not None,
        }

    @app.websocket("/ws/services/{service_id}")
    async def websocket_endpoint(websocket: WebSocket, service_id: str):
        """WebSocket endpoint for real-time updates."""
        if not state.websocket_handler:
            await websocket.close(code=1011, reason="WebSocket handler not initialized")
            return

        await state.websocket_handler.connect(service_id, websocket)

        try:
            while True:
                # Keep connection alive
                await websocket.receive_text()

        except WebSocketDisconnect:
            await state.websocket_handler.disconnect(service_id)

        except Exception as e:
            logger.error("WebSocket error", service_id=service_id, error=str(e))
            await state.websocket_handler.disconnect(service_id)

    # Legacy AutoQA endpoints would be included here
    # ...

    return app


def run_server(host: str = "0.0.0.0", port: int = 8080) -> None:
    """Run the API server."""
    import uvicorn

    uvicorn.run(
        "autoqa.api.main:create_app",
        host=host,
        port=port,
        factory=True,
        reload=os.environ.get("DEBUG", "false").lower() == "true",
    )


if __name__ == "__main__":
    run_server()
