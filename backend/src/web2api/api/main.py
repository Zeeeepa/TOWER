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
from fastapi import FastAPI, File, HTTPException, Query, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from web2api.orchestrator.scheduler import JobPriority, JobStatus, TestOrchestrator
from web2api.storage.artifact_manager import ArtifactManager
from web2api.storage.database import DatabaseManager, TestResultRepository
from owl_browser import Browser, RemoteConfig, BrowserConfig
from web2api.execution.queue_manager import ExecutionQueue
from web2api.api.websocket_handler import WebSocketHandler
from web2api.api.openai_compat import set_container, router as openai_router
from web2api.concurrency.browser_pool import BrowserPool
from web2api.concurrency.config import ConcurrencyConfig

logger = structlog.get_logger(__name__)


class SubmitJobRequest(BaseModel):
    """Request to submit a test job."""

    test_spec_path: str | None = None
    test_spec_content: str | None = None
    suite_name: str | None = None
    test_name: str | None = None
    priority: str = "normal"
    timeout_seconds: int | None = None
    environment: str | None = None
    variables: dict[str, str] = Field(default_factory=dict)
    tags: list[str] = Field(default_factory=list)
    ci_metadata: dict[str, str] = Field(default_factory=dict)


class JobResponse(BaseModel):
    """Response containing job information."""

    id: str
    status: str
    suite_name: str | None = None
    test_name: str | None = None
    priority: str
    created_at: datetime
    started_at: datetime | None = None
    finished_at: datetime | None = None
    worker_id: str | None = None
    error: str | None = None


class TestRunResponse(BaseModel):
    """Response containing test run information."""

    id: str
    test_name: str
    suite_name: str | None = None
    status: str
    started_at: datetime
    finished_at: datetime | None = None
    duration_ms: int
    total_steps: int
    passed_steps: int
    failed_steps: int
    healed_steps: int
    video_path: str | None = None
    error: str | None = None


class StatsResponse(BaseModel):
    """Response containing statistics."""

    queue_length: int
    total_workers: int
    active_workers: int
    max_concurrent_jobs: int


class HealthResponse(BaseModel):
    """Health check response."""

    status: str
    timestamp: datetime
    version: str


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
    # Support both LOCAL and REMOTE modes:
    # - LOCAL: Set OWL_BROWSER_PATH to the browser binary path
    # - REMOTE: Set OWL_BROWSER_URL to the HTTP server URL
    owl_browser_url = os.environ.get("OWL_BROWSER_URL")
    owl_browser_token = os.environ.get("OWL_BROWSER_TOKEN")
    owl_browser_path = os.environ.get("OWL_BROWSER_PATH")

    if owl_browser_url or owl_browser_path:
        try:
            if owl_browser_url:
                # Remote mode: connect to HTTP server
                log.info("Connecting to remote owl-browser", url=owl_browser_url)
                remote_config = RemoteConfig(url=owl_browser_url, token=owl_browser_token)
                state.browser = Browser(remote=remote_config)
            else:
                # Local mode: launch local browser binary
                log.info("Launching local owl-browser", path=owl_browser_path)
                state.browser = Browser(
                    browser_path=owl_browser_path,
                    headless=os.environ.get("OWL_BROWSER_HEADLESS", "true").lower() == "true",
                    verbose=os.environ.get("OWL_BROWSER_VERBOSE", "false").lower() == "true",
                )
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
            "web2api_ready": state.orchestrator is not None,
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

    @app.post("/api/v1/jobs", response_model=JobResponse)
    async def submit_job(request: SubmitJobRequest) -> JobResponse:
        """Submit a test job for execution."""
        if state.orchestrator is None:
            raise HTTPException(status_code=503, detail="Orchestrator not available")

        if not request.test_spec_path and not request.test_spec_content:
            raise HTTPException(
                status_code=400,
                detail="Either test_spec_path or test_spec_content is required",
            )

        try:
            priority = JobPriority(request.priority)
        except ValueError:
            raise HTTPException(
                status_code=400,
                detail=f"Invalid priority: {request.priority}",
            )

        job_id = await state.orchestrator.submit_job(
            test_spec_path=request.test_spec_path,
            test_spec_content=request.test_spec_content,
            suite_name=request.suite_name,
            test_name=request.test_name,
            priority=priority,
            timeout_seconds=request.timeout_seconds,
            environment=request.environment,
            variables=request.variables,
            tags=request.tags,
            ci_metadata=request.ci_metadata,
        )

        job = await state.orchestrator.get_job(job_id)
        if job is None:
            raise HTTPException(status_code=500, detail="Job creation failed")

        return JobResponse(
            id=job.id,
            status=job.status,
            suite_name=job.suite_name,
            test_name=job.test_name,
            priority=job.priority,
            created_at=job.created_at,
            started_at=job.started_at,
            finished_at=job.finished_at,
            worker_id=job.worker_id,
            error=job.error,
        )

    @app.get("/api/v1/jobs/{job_id}", response_model=JobResponse)
    async def get_job(job_id: str) -> JobResponse:
        """Get job details by ID."""
        if state.orchestrator is None:
            raise HTTPException(status_code=503, detail="Orchestrator not available")

        job = await state.orchestrator.get_job(job_id)
        if job is None:
            raise HTTPException(status_code=404, detail="Job not found")

        return JobResponse(
            id=job.id,
            status=job.status,
            suite_name=job.suite_name,
            test_name=job.test_name,
            priority=job.priority,
            created_at=job.created_at,
            started_at=job.started_at,
            finished_at=job.finished_at,
            worker_id=job.worker_id,
            error=job.error,
        )

    @app.delete("/api/v1/jobs/{job_id}")
    async def cancel_job(job_id: str) -> dict[str, Any]:
        """Cancel a pending or running job."""
        if state.orchestrator is None:
            raise HTTPException(status_code=503, detail="Orchestrator not available")

        cancelled = await state.orchestrator.cancel_job(job_id)
        if not cancelled:
            raise HTTPException(
                status_code=400,
                detail="Job cannot be cancelled (not found or not cancellable)",
            )

        return {"status": "cancelled", "job_id": job_id}

    @app.post("/api/v1/jobs/{job_id}/retry")
    async def retry_job(job_id: str) -> dict[str, Any]:
        """Retry a failed job."""
        if state.orchestrator is None:
            raise HTTPException(status_code=503, detail="Orchestrator not available")

        retried = await state.orchestrator.retry_job(job_id)
        if not retried:
            raise HTTPException(
                status_code=400,
                detail="Job cannot be retried (max retries exceeded or not found)",
            )

        return {"status": "requeued", "job_id": job_id}

    @app.get("/api/v1/jobs")
    async def list_jobs(
        status: str | None = Query(None),
        limit: int = Query(100, ge=1, le=1000),
    ) -> list[JobResponse]:
        """List jobs with optional status filter."""
        if state.orchestrator is None:
            raise HTTPException(status_code=503, detail="Orchestrator not available")

        job_status = None
        if status:
            try:
                job_status = JobStatus(status)
            except ValueError:
                raise HTTPException(
                    status_code=400,
                    detail=f"Invalid status: {status}",
                )

        jobs = await state.orchestrator.list_jobs(status=job_status, limit=limit)

        return [
            JobResponse(
                id=job.id,
                status=job.status,
                suite_name=job.suite_name,
                test_name=job.test_name,
                priority=job.priority,
                created_at=job.created_at,
                started_at=job.started_at,
                finished_at=job.finished_at,
                worker_id=job.worker_id,
                error=job.error,
            )
            for job in jobs
        ]

    @app.get("/api/v1/stats", response_model=StatsResponse)
    async def get_stats() -> StatsResponse:
        """Get queue and worker statistics."""
        if state.orchestrator is None:
            raise HTTPException(status_code=503, detail="Orchestrator not available")

        stats = await state.orchestrator.get_queue_stats()
        return StatsResponse(**stats)

    @app.get("/api/v1/runs")
    async def list_test_runs(
        suite_name: str | None = Query(None),
        status: str | None = Query(None),
        environment: str | None = Query(None),
        branch: str | None = Query(None),
        limit: int = Query(100, ge=1, le=1000),
        offset: int = Query(0, ge=0),
    ) -> list[dict[str, Any]]:
        """List test runs with filtering."""
        if state.repository is None:
            raise HTTPException(status_code=503, detail="Database not available")

        return await state.repository.list_test_runs(
            suite_name=suite_name,
            status=status,
            environment=environment,
            branch=branch,
            limit=limit,
            offset=offset,
        )

    @app.get("/api/v1/runs/{run_id}")
    async def get_test_run(run_id: str) -> dict[str, Any]:
        """Get test run details by ID."""
        if state.repository is None:
            raise HTTPException(status_code=503, detail="Database not available")

        run = await state.repository.get_test_run(run_id)
        if run is None:
            raise HTTPException(status_code=404, detail="Test run not found")

        return run

    @app.get("/api/v1/runs/{run_id}/steps")
    async def get_step_results(run_id: str) -> list[dict[str, Any]]:
        """Get step results for a test run."""
        if state.repository is None:
            raise HTTPException(status_code=503, detail="Database not available")

        return await state.repository.get_step_results(run_id)

    @app.get("/api/v1/runs/{run_id}/artifacts")
    async def list_artifacts(run_id: str) -> list[str]:
        """List artifacts for a test run."""
        if state.artifact_manager is None:
            raise HTTPException(status_code=503, detail="Artifact manager not available")

        return state.artifact_manager.list_artifacts(run_id)

    @app.post("/api/v1/validate")
    async def validate_spec(
        file: UploadFile = File(...),
    ) -> dict[str, Any]:
        """Validate a test specification file."""
        from web2api.dsl.parser import DSLParseError, DSLParser

        content = await file.read()
        parser = DSLParser()

        try:
            spec = parser.parse_string(content.decode("utf-8"))
            return {
                "valid": True,
                "name": spec.name,
                "steps": len(spec.steps) if hasattr(spec, "steps") else len(spec.tests),
            }
        except DSLParseError as e:
            return {
                "valid": False,
                "error": str(e),
            }

    @app.get("/api/v1/statistics")
    async def get_statistics(
        suite_name: str | None = Query(None),
        days: int = Query(30, ge=1, le=365),
    ) -> dict[str, Any]:
        """Get test execution statistics."""
        if state.repository is None:
            raise HTTPException(status_code=503, detail="Database not available")

        return await state.repository.get_test_statistics(
            suite_name=suite_name,
            days=days,
        )

    @app.post("/api/v1/build")
    async def build_test_spec(
        url: str = Query(..., description="Starting page URL to analyze"),
        username: str | None = Query(None, description="Username for authentication"),
        password: str | None = Query(None, description="Password for authentication"),
        depth: int = Query(1, ge=0, le=5, description="Crawl depth for same-domain pages"),
        max_pages: int = Query(10, ge=1, le=50, description="Maximum pages to analyze"),
        include_hidden: bool = Query(False, description="Include hidden elements"),
        timeout_ms: int = Query(30000, ge=1000, le=120000, description="Timeout in ms"),
    ) -> dict[str, Any]:
        """
        Auto-generate YAML test specification from a webpage.

        Analyzes the specified URL (and optionally crawls same-domain links)
        to discover interactive elements and generate a complete test spec.
        """
        from owl_browser import Browser, RemoteConfig

        from web2api.builder.test_builder import AutoTestBuilder, BuilderConfig

        owl_browser_url = os.environ.get("OWL_BROWSER_URL")
        owl_browser_token = os.environ.get("OWL_BROWSER_TOKEN")

        if not owl_browser_url:
            raise HTTPException(
                status_code=503,
                detail="OWL_BROWSER_URL not configured",
            )

        try:
            remote_config = RemoteConfig(url=owl_browser_url, token=owl_browser_token)
            browser = Browser(remote=remote_config)
            browser.launch()

            try:
                config = BuilderConfig(
                    url=url,
                    username=username,
                    password=password,
                    depth=depth,
                    max_pages=max_pages,
                    include_hidden=include_hidden,
                    timeout_ms=timeout_ms,
                )

                builder = AutoTestBuilder(browser=browser, config=config)
                yaml_content = builder.build()

                # Extract summary from page analyses
                pages_analyzed = len(builder._page_analyses)
                total_elements = sum(len(p.elements) for p in builder._page_analyses)
                has_login = any(p.has_login_form for p in builder._page_analyses)

                return {
                    "success": True,
                    "yaml_content": yaml_content,
                    "summary": {
                        "url": url,
                        "pages_analyzed": pages_analyzed,
                        "total_elements": total_elements,
                        "has_login_form": has_login,
                        "depth": depth,
                    },
                }

            finally:
                browser.close()

        except Exception as e:
            logger.error("Build failed", url=url, error=str(e))
            raise HTTPException(
                status_code=500,
                detail=f"Failed to build test specification: {e}",
            ) from e

    # Include service management routes
    from web2api.api.service_manager import router as service_router
    app.include_router(service_router)

    return app


def run_server(host: str = "0.0.0.0", port: int = 8080) -> None:
    """Run the API server."""
    import uvicorn

    uvicorn.run(
        "web2api.api.main:create_app",
        host=host,
        port=port,
        factory=True,
        reload=os.environ.get("DEBUG", "false").lower() == "true",
    )


if __name__ == "__main__":
    run_server()
