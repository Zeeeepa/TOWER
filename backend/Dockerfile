# ==============================================================================
# TOWER Web2API Backend - Dockerfile
# ==============================================================================
# Multi-stage build for the Python backend API server.
#
# Build: docker build -t tower-backend:latest -f Dockerfile .
# Run:   docker run -p 8000:8000 tower-backend:latest
# ==============================================================================

# -----------------------------------------------------------------------------
# Stage 1: Builder
# -----------------------------------------------------------------------------
FROM python:3.12-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    curl \
    libpq-dev \
    # OpenCV dependencies
    libgl1-mesa-glx \
    libglib2.0-0 \
    # Tesseract for OCR
    tesseract-ocr \
    libtesseract-dev \
    && rm -rf /var/lib/apt/lists/*

# Create virtual environment
RUN python -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# Install Python dependencies
WORKDIR /build
COPY pyproject.toml ./

# Install pip-tools and compile dependencies
RUN pip install --no-cache-dir --upgrade pip setuptools wheel

# Install the package dependencies (without owl-browser, we'll mock it)
RUN pip install --no-cache-dir \
    fastapi>=0.115.0 \
    uvicorn[standard]>=0.32.0 \
    pydantic>=2.10.0 \
    pydantic-settings>=2.6.0 \
    pyyaml>=6.0.2 \
    jinja2>=3.1.4 \
    httpx>=0.28.0 \
    boto3>=1.35.0 \
    asyncpg>=0.30.0 \
    "sqlalchemy[asyncio]>=2.0.36" \
    alembic>=1.14.0 \
    pillow>=11.0.0 \
    imagehash>=4.3.1 \
    scikit-image>=0.24.0 \
    numpy>=2.1.0 \
    structlog>=24.4.0 \
    tenacity>=9.0.0 \
    aiofiles>=24.1.0 \
    python-multipart>=0.0.17 \
    orjson>=3.10.0 \
    redis>=5.2.0 \
    kubernetes>=31.0.0 \
    python-dotenv>=1.0.0 \
    opencv-python>=4.10.0 \
    easyocr>=1.7.0 \
    pytesseract>=0.3.10 \
    scikit-learn>=1.5.0 \
    scipy>=1.14.0 \
    cryptography>=42.0.0 \
    websockets>=12.0

# -----------------------------------------------------------------------------
# Stage 2: Runtime
# -----------------------------------------------------------------------------
FROM python:3.12-slim AS runtime

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    libgl1-mesa-glx \
    libglib2.0-0 \
    tesseract-ocr \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Copy virtual environment from builder
COPY --from=builder /opt/venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# Create app directory
WORKDIR /app

# Copy source code
COPY src/ /app/src/
COPY .web2api/ /app/.web2api/

# Set Python path to include src directory
ENV PYTHONPATH="/app/src:${PYTHONPATH}"

# Create non-root user
RUN useradd --create-home --shell /bin/bash appuser && \
    chown -R appuser:appuser /app
USER appuser

# Environment defaults
ENV API_HOST=0.0.0.0
ENV API_PORT=8000

# Expose port
EXPOSE 8000

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:${API_PORT}/health || exit 1

# Run the API server using factory pattern
CMD ["python", "-m", "uvicorn", "web2api.api.main:create_app", "--factory", "--host", "0.0.0.0", "--port", "8000"]
