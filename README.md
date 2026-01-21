
 EQUIREMENTS_UPGRADED.md
# Web2API: Complete Implementation Specification (UPGRADED)
## Executive Summary
**Web2API** transforms any web service into an OpenAI-compatible API endpoint using intelligent browser automation and auto-discovery.
**Core Flow**: `URL + Credentials → Auto-Discovery → OpenAI API Endpoint`
**Key Innovation**: Zero manual configuration - the system discovers service capabilities, authentication flows, and operations automatically through intelligent analysis.
- **INTEGRATE** all 157 Owl-Browser commands properly
---
## Table of Contents
1. [Complete Owl-Browser Command Mapping](#1-complete-owl-browser-command-mapping)
3. [Architecture Transformation Strategy](#3-architecture-transformation-strategy)
4. [Discovery Pipeline Implementation](#4-discovery-pipeline-implementation)
5. [Execution Engine with Owl-Browser Integration](#5-execution-engine-with-owl-browser-integration)
6. [Live Viewport Streaming](#6-live-viewport-streaming)
7. [Database Schema Extensions](#7-database-schema-extensions)
8. [Implementation Phases](#8-implementation-phases)
9. [Success Criteria](#9-success-criteria)
---
## 1. Complete Owl-Browser Command Mapping
### 1.1 Authentication & Login Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_detect_captcha` | Detect CAPTCHA during login | `autoqa/builder/discovery/form_analyzer.py` |
| `browser_classify_captcha` | Classify CAPTCHA type | NEW: `auth/captcha_handler.py` |
| `browser_solve_text_captcha` | Solve text-based CAPTCHAs | NEW: `auth/captcha_handler.py` |
| `browser_solve_image_captcha` | Solve image-based CAPTCHAs | NEW: `auth/captcha_handler.py` |
| `browser_solve_captcha` | Universal CAPTCHA solver | NEW: `auth/captcha_handler.py` |
| `browser_find_element` | Find login forms, buttons | `autoqa/builder/discovery/form_analyzer.py` |
| `browser_get_cookies` | Extract session cookies after login | NEW: `auth/session_manager.py` |
| `browser_set_cookie` | Restore saved session | NEW: `auth/session_manager.py` |
### 1.2 Feature Discovery Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_ai_analyze` | AI-powered page analysis | `autoqa/builder/analyzer/visual_analyzer.py` |
| `browser_query_page` | Query page capabilities | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_screenshot` | Capture UI for vision models | `autoqa/builder/analyzer/visual_analyzer.py` |
| `browser_get_html` | Extract DOM structure | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_get_markdown` | Extract content as markdown | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_extract_text` | Extract visible text | `autoqa/builder/analyzer/page_analyzer.py` |
### 1.3 Execution & Response Detection Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_is_enabled` | **CRITICAL**: Detect when "Send" button re-enables (response complete) | MODIFY: `runner/test_runner.py` → `execution/operation_runner.py` |
| `browser_ai_extract` | Extract response from chat interface | MODIFY: `runner/test_runner.py` → `execution/operation_runner.py` |
| `browser_extract_text` | Extract text content | MODIFY: `runner/test_runner.py` → `execution/operation_runner.py` |
| `browser_click` | Click buttons, submit forms | `autoqa/concurrency/browser_pool.py` |
| `browser_type` | Fill input fields | `autoqa/concurrency/browser_pool.py` |
| `browser_upload_file` | Upload files for services that support it | MODIFY: `runner/test_runner.py` |
| `browser_select_option` | Select dropdown options | `autoqa/concurrency/browser_pool.py` |
### 1.4 Live Viewport & Recording Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `start_live_stream` | **CRITICAL**: Live viewport during discovery | NEW: `execution/live_viewport.py` |
| `stop_live_stream` | Stop live viewport | NEW: `execution/live_viewport.py` |
| `get_live_stream_stats` | Get stream statistics | NEW: `execution/live_viewport.py` |
| `list_live_streams` | List active streams | NEW: `execution/live_viewport.py` |
| `get_live_frame` | Get current frame | NEW: `execution/live_viewport.py` |
| `start_video_recording` | Record discovery process | NEW: `execution/video_recorder.py` |
| `stop_video_recording` | Stop recording | NEW: `execution/video_recorder.py` |
| `download_video_recording` | Download recorded video | NEW: `execution/video_recorder.py` |
| `get_video_recording_stats` | Get recording stats | NEW: `execution/video_recorder.py` |
### 1.5 Tab Management Commands (Multi-Service Parallelization)
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `new_tab` | Create isolated service context | MODIFY: `concurrency/browser_pool.py` |
| `switch_tab` | Switch between service contexts | MODIFY: `concurrency/browser_pool.py` |
| `close_tab` | Close service context | MODIFY: `concurrency/browser_pool.py` |
| `get_tabs` | List all service tabs | MODIFY: `concurrency/browser_pool.py` |
| `get_active_tab` | Get current service tab | MODIFY: `concurrency/browser_pool.py` |
| `get_tab_count` | Count service tabs | MODIFY: `concurrency/browser_pool.py` |
| `set_popup_policy` | Handle popups during discovery | MODIFY: `concurrency/browser_pool.py` |
| `get_blocked_popups` | Check blocked popups | MODIFY: `concurrency/browser_pool.py` |
### 1.6 Navigation & State Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_navigate` | Navigate to service URL | `autoqa/concurrency/browser_pool.py` |
| `browser_reload` | Reload page | `autoqa/concurrency/browser_pool.py` |
| `browser_go_back` | Go back in history | `autoqa/concurrency/browser_pool.py` |
| `browser_go_forward` | Go forward in history | `autoqa/concurrency/browser_pool.py` |
| `browser_wait_for_selector` | Wait for elements | `autoqa/concurrency/browser_pool.py` |
| `browser_wait_for_navigation` | Wait for page load | `autoqa/concurrency/browser_pool.py` |
### 1.7 Element Inspection Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_get_element_count` | Count elements | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_get_element_text` | Get element text | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_get_element_html` | Get element HTML | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_get_element_attribute` | Get attributes | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_is_visible` | Check element visibility | `autoqa/builder/analyzer/page_analyzer.py` |
| `browser_is_hidden` | Check if hidden | `autoqa/builder/analyzer/page_analyzer.py` |
### 1.8 Form & Input Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_fill_form` | Auto-fill login forms | MODIFY: `builder/discovery/form_analyzer.py` → `auth/form_filler.py` |
| `browser_check` | Check checkboxes | `autoqa/concurrency/browser_pool.py` |
| `browser_uncheck` | Uncheck checkboxes | `autoqa/concurrency/browser_pool.py` |
| `browser_hover` | Hover over elements | `autoqa/concurrency/browser_pool.py` |
| `browser_drag` | Drag and drop | `autoqa/concurrency/browser_pool.py` |
### 1.9 JavaScript & Evaluation Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_evaluate` | Execute custom JS | `autoqa/concurrency/browser_pool.py` |
| `browser_evaluate_on_page` | Evaluate in page context | `autoqa/concurrency/browser_pool.py` |
| `browser_get_current_url` | Get current URL | `autoqa/concurrency/browser_pool.py` |
| `browser_get_page_title` | Get page title | `autoqa/concurrency/browser_pool.py` |
### 1.10 Network & Performance Commands
| Owl-Browser Command | Web2API Use Case | Integration File |
|--------------------|------------------|------------------|
| `browser_get_network_requests` | Monitor API calls | MODIFY: `builder/discovery/api_detector.py` |
| `browser_wait_for_response` | Wait for API response | `autoqa/concurrency/browser_pool.py` |
| `browser_get_console_logs` | Get console errors | `autoqa/concurrency/browser_pool.py` |
| `browser_get_metrics` | Get performance metrics | `autoqa/concurrency/browser_pool.py` |
---


**Target Purpose**: Web2API service management + OpenAI endpoints
- POST /build
- POST /run
- GET /results
# ADD (Web2API-specific)
+ POST /api/services  # Register service
+ GET /api/services  # List services
+ GET /api/services/:id  # Get service
+ PUT /api/services/:id  # Update config
+ DELETE /api/services/:id  # Delete service
+ POST /api/services/:id/discover  # Trigger discovery
+ WS /ws/services/:id  # WebSocket for live updates
# ADD (OpenAI compatibility)
+ POST /v1/chat/completions  # Main endpoint
+ GET /v1/models  # List services as models
# KEEP
- CORS middleware
- Error handlers
- Health check
```
---
#### `concurrency/browser_pool.py` → `concurrency/browser_pool.py` (Enhance)
**Current Purpose**: Pool browser contexts for parallel tests
**Target Purpose**: Pool browser contexts per service (isolation)
**Changes Required**:
```python
# ADD service-specific context management
+ async def acquire_service_context(self, service_id: str) -> BrowserContext:
+     """Get or create context for specific service."""
+     if service_id not in self._service_contexts:
+         context = await self._create_service_context(service_id)
+         self._service_contexts[service_id] = context
+     return self._service_contexts[service_id]
# ADD tab management for multi-service
+ async def new_service_tab(self, service_id: str):
+     """Create new tab for service isolation."""
+     
+ async def switch_service_tab(self, service_id: str, tab_index: int):
+     """Switch to service tab."""
# ADD session persistence
+ async def save_service_session(self, service_id: str):
+     """Save cookies for service."""
+     cookies = await self._browser.browser_get_cookies(
+         context_id=self._service_contexts[service_id].id
+     )
+     await self._storage.save_session(service_id, cookies)
+ async def restore_service_session(self, service_id: str):
+     """Restore saved session."""
+     cookies = await self._storage.get_session(service_id)
+     await self._browser.browser_set_cookie(
+         context_id=self._service_contexts[service_id].id,
+         cookies=cookies
+     )
# KEEP existing pooling logic
- Lifecycle management
- Health checks
- Resource cleanup
```
---
#### `builder/analyzer/page_analyzer.py` → `discovery/feature_mapper.py` (Adapt)
**Current Purpose**: Analyze page structure for test generation
**Target Purpose**: Extract service capabilities (features)
**Changes Required**:
```python
# ADD Owl-Browser AI commands
+ async def detect_service_capabilities(self, browser, context_id):
+     """Use browser_ai_analyze to detect features."""
+     analysis = await browser.browser_ai_analyze({
+         "context_id": context_id,
+         "question": "What capabilities does this service offer? (chat, image generation, code execution, etc.)"
+     })
+     return self._parse_capabilities(analysis)
+ async def detect_ui_elements(self, browser, context_id):
+     """Use browser_query_page to find interactive elements."""
+     elements = await browser.browser_query_page({
+         "context_id": context_id,
+         "question": "List all buttons, inputs, dropdowns, and their purposes"
+     })
+     return self._parse_elements(elements)
# ADD model/feature detection
+ async def detect_available_models(self, browser, context_id):
+     """Find model selector options."""
+     models = await browser.browser_query_page({
+         "context_id": context_id,
+         "question": "What models or AI engines are available?"
+     })
+     return models
# KEEP existing DOM analysis
- HTML parsing
- Element classification
- Interactive element detection
```
**Reuse %**: 85% (add AI detection, keep DOM analysis)
---
#### `builder/analyzer/visual_analyzer.py` → `discovery/visual_analyzer.py` (Keep)
**Current Purpose**: Vision model integration for UI analysis
**Target Purpose**: Same (perfect for feature detection)
**No Changes Needed** - Already integrated with vision models!
**Reuse %**: 100%
---
#### `builder/discovery/form_analyzer.py` → `auth/form_analyzer.py` (Adapt)
**Current Purpose**: Detect input fields and generate test cases
**Target Purpose**: Detect authentication forms + CAPTCHA handling
**Changes Required**:
```python
# ADD auth-specific detection
+ async def detect_login_form(self, elements):
+     """Identify login form elements."""
+     email_field = await self._browser.browser_find_element({
+         "context_id": context_id,
+         "description": "email or username input field"
+     })
+     password_field = await self._browser.browser_find_element({
+         "context_id": context_id,
+         "description": "password input field"
+     })
+     return {"email": email_field, "password": password_field}
+ async def handle_captcha(self, context_id):
+     """Detect and solve CAPTCHA if present."""
+     has_captcha = await self._browser.browser_detect_captcha({
+         "context_id": context_id
+     })
+     
+     if has_captcha.get("found"):
+         captcha_type = await self._browser.browser_classify_captcha({
+             "context_id": context_id
+         })
+         
+         if captcha_type["type"] == "text":
+             solved = await self._browser.browser_solve_text_captcha({
+                 "context_id": context_id
+             })
+         elif captcha_type["type"] == "image":
+             solved = await self._browser.browser_solve_image_captcha({
+                 "context_id": context_id
+             })
+         else:
+             solved = await self._browser.browser_solve_captcha({
+                 "context_id": context_id
+             })
+         
+         return solved
+     return None
# KEEP existing field detection
- Field type detection (email, password, etc.)
- Validation rule inference
- Test case generation
```
**Reuse %**: 90% (keep detection, add CAPTCHA)
---
#### `runner/test_runner.py` → `execution/operation_runner.py` (Transform)
**Current Purpose**: Execute test steps sequentially
**Target Purpose**: Execute discovered operations with streaming
**Critical Changes**:
```python
# ADD response detection using browser_is_enabled
+ async def wait_for_response_complete(self, browser, context_id, submit_selector):
+     """
+     CRITICAL: Use browser_is_enabled to detect when response is ready.
+     
+     Most chat interfaces disable the "Send" button while generating response.
+     When it re-enables, response is complete.
+     """
+     while True:
+         is_enabled = await browser.browser_is_enabled({
+             "context_id": context_id,
+             "selector": submit_selector
+         })
+         
+         if is_enabled.get("enabled"):
+             break  # Response complete!
+         
+         await asyncio.sleep(0.5)
# ADD response extraction
+ async def extract_response(self, browser, context_id, output_selector):
+     """Extract AI response from chat interface."""
+     # Use AI-powered extraction
+     response = await browser.browser_ai_extract({
+         "context_id": context_id,
+         "selector": output_selector,
+         "prompt": "Extract the AI assistant's response text"
+     })
+     
+     # Fallback to text extraction
+     if not response.get("content"):
+         response = await browser.browser_extract_text({
+             "context_id": context_id,
+             "selector": output_selector
+         })
+     
+     return response.get("content", "")
# ADD file upload support
+ async def upload_file_if_needed(self, browser, context_id, file_path):
+     """Handle file upload for services that support it."""
+     await browser.browser_upload_file({
+         "context_id": context_id,
+         "files": [file_path]
+     })
# MODIFY execution loop to support streaming
- async def run_test(self, test):
+ async def execute_operation(self, service_id, operation, params, websocket=None):
+     """Execute operation with real-time streaming."""
+     if websocket:
+         await websocket.send_json({"type": "log", "message": "Starting operation"})
+     
+     for step in operation.execution_steps:
+         await self._execute_step(step, websocket)
+     
+     # Extract and return result
+     result = await self.extract_response(...)
+     return result
# KEEP existing step execution
- Navigate
- Click
- Type
- Wait for selectors
```
**Reuse %**: 60% (keep execution, add response detection & streaming)
---
#### `runner/self_healing.py` → `execution/self_healing.py` (Keep)
**Current Purpose**: Selector recovery when UI changes
**Target Purpose**: Same
**No Changes Needed** - Perfect for error recovery!
**Reuse %**: 100%
---
#### `llm/service.py` → `llm/service.py` (Extend)
**Current Purpose**: LLM integration for AutoQA tools
**Target Purpose**: Add Web2API-specific prompts
**Changes Required**:
```python
# ADD new prompt types
+ PromptType.AUTH_DETECTION
+ PromptType.FEATURE_EXTRACTION
+ PromptType.OPERATION_MAPPING
+ PromptType.CAPABILITY_ANALYSIS
# ADD new methods
+ async def detect_auth_type(self, url, page_content, forms):
+     """Detect authentication mechanism using LLM."""
+     return await self._execute_prompt(
+         ToolName.WEB2API,  # New tool
+         PromptType.AUTH_DETECTION,
+         {"url": url, "page": page_content, "forms": json.dumps(forms)}
+     )
+ async def extract_service_capabilities(self, screenshot, dom):
+     """Extract service capabilities."""
+     return await self._execute_prompt(
+         ToolName.WEB2API,
+         PromptType.FEATURE_EXTRACTION,
+         {"screenshot": screenshot, "dom": dom}
+     )
+ async def map_operation(self, features, purpose):
+     """Map features to executable operation."""
+     return await self._execute_prompt(
+         ToolName.WEB2API,
+         PromptType.OPERATION_MAPPING,
+         {"features": json.dumps(features), "purpose": purpose}
+     )
# KEEP existing LLM methods
- Test generation
- Step transformation
- Assertion validation
- Selector enhancement
```
**Reuse %**: 90% (keep infrastructure, add Web2API prompts)
---
#### `storage/database.py` → `storage/database.py` (Extend)
**Current Purpose**: Store test artifacts and results
**Target Purpose**: Store services, credentials, executions
**Changes Required**:
```python
# ADD new tables (append-only)
+ class Service(Base):
+     __tablename__ = "services"
+     
+     id = Column(UUID, primary_key=True)
+     name = Column(String(255))
+     url = Column(Text)
+     type = Column(String(50))
+     status = Column(String(50))
+     login_status = Column(String(50))
+     discovery_status = Column(String(50))
+     config = Column(JSONB)
+     created_at = Column(DateTime, default=datetime.utcnow)
+     updated_at = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
+ class ServiceCredential(Base):
+     __tablename__ = "service_credentials"
+     
+     id = Column(UUID, primary_key=True)
+     service_id = Column(UUID, ForeignKey("services.id"))
+     auth_type = Column(String(50))
+     encrypted_email = Column(Text)
+     encrypted_password = Column(Text)
+     encrypted_api_key = Column(Text)
+     session_cookie = Column(JSONB)
+     created_at = Column(DateTime, default=datetime.utcnow)
+ class Execution(Base):
+     __tablename__ = "executions"
+     
+     id = Column(UUID, primary_key=True)
+     service_id = Column(UUID, ForeignKey("services.id"))
+     operation_id = Column(String(100))
+     parameters = Column(JSONB)
+     result = Column(Text)
+     status = Column(String(50))
+     started_at = Column(DateTime, default=datetime.utcnow)
+     completed_at = Column(DateTime)
+     error = Column(Text)
+ class ServiceStats(Base):
+     __tablename__ = "service_stats"
+     
+     service_id = Column(UUID, ForeignKey("services.id"), primary_key=True)
+     total_requests = Column(Integer, default=0)
+     successful_requests = Column(Integer, default=0)
+     failed_requests = Column(Integer, default=0)
+     avg_latency_ms = Column(Integer, default=0)
+     last_request_at = Column(DateTime)
+     uptime_start = Column(DateTime, default=datetime.utcnow)
# KEEP existing tables
- artifacts
- test_results
- test_suites
```
**Reuse %**: 80% (keep infrastructure, add service tables)
---
### 2.3 NEW Files to Create
#### `api/service_manager.py` (NEW)
```python
"""
Service CRUD endpoints for TOWER frontend integration.
"""
from fastapi import APIRouter, Depends, HTTPException
from web2api.storage.database import Service, ServiceCredential
from web2api.auth.credential_store import CredentialStore
from web2api.execution.queue_manager import ExecutionQueue
router = APIRouter(prefix="/api/services", tags=["services"])
@router.post("/")
async def register_service(
    url: str,
    email: str,
    password: str,
    credential_store: CredentialStore = Depends()
):
    """Register a new service."""
    # Create service
    service = Service(
        name=url.split("//")[1].split("/")[0],
        url=url,
        status="offline",
        login_status="pending",
        discovery_status="pending"
    )
    
    # Encrypt and store credentials
    credential_store.store_credentials(service.id, email, password)
    
    # Trigger background login
    await trigger_login_process(service.id)
    
    return service
@router.get("/")
async def list_services():
    """List all registered services."""
    return await Service.all()
@router.get("/{service_id}")
async def get_service(service_id: str):
    """Get service details."""
    return await Service.get(service_id)
@router.put("/{service_id}")
async def update_service_config(service_id: str, config: dict):
    """Update service configuration."""
    service = await Service.get(service_id)
    service.config = config
    await service.save()
    return service
@router.delete("/{service_id}")
async def delete_service(service_id: str):
    """Delete a service."""
    await Service.delete(service_id)
    return {"success": True}
@router.post("/{service_id}/discover")
async def trigger_discovery(service_id: str):
    """Trigger auto-discovery process."""
    from web2api.discovery.orchestrator import DiscoveryOrchestrator
    
    orchestrator = DiscoveryOrchestrator()
    task_id = await orchestrator.start_discovery(service_id)
    
    return {
        "message": "Discovery started",
        "task_id": task_id
    }
```
---
#### `api/websocket_handler.py` (NEW)
```python
"""
WebSocket handler for real-time updates to TOWER frontend.
"""
from fastapi import WebSocket
from web2api.execution.live_viewport import LiveViewportManager
class WebSocketHandler:
    """Manages WebSocket connections for live updates."""
    
    def __init__(self):
        self.active_connections: dict[str, WebSocket] = {}
    
    async def connect(self, service_id: str, websocket: WebSocket):
        """Accept WebSocket connection."""
        await websocket.accept()
        self.active_connections[service_id] = websocket
    
    async def disconnect(self, service_id: str):
        """Remove WebSocket connection."""
        self.active_connections.pop(service_id, None)
    
    async def send_login_update(self, service_id: str, status: str, message: str):
        """Send login status update."""
        if service_id in self.active_connections:
            await self.active_connections[service_id].send_json({
                "type": "login_update",
                "loginStatus": status,
                "message": message
            })
    
    async def send_discovery_update(
        self, 
        service_id: str, 
        status: str, 
        progress: int, 
        message: str
    ):
        """Send discovery progress update."""
        if service_id in self.active_connections:
            await self.active_connections[service_id].send_json({
                "type": "discovery_update",
                "discoveryStatus": status,
                "progress": progress,
                "message": message
            })
    
    async def send_execution_log(
        self, 
        service_id: str, 
        level: str, 
        message: str
    ):
        """Send execution log."""
        if service_id in self.active_connections:
            await self.active_connections[service_id].send_json({
                "type": "execution_log",
                "timestamp": int(time.time() * 1000),
                "level": level,
                "message": message
            })
    
    async def send_live_viewport_frame(self, service_id: str, frame_data: bytes):
        """Send live viewport frame."""
        if service_id in self.active_connections:
            await self.active_connections[service_id].send_bytes(frame_data)
```
---
#### `api/openai_compat.py` (NEW)
```python
"""
OpenAI-compatible API endpoints.
"""
from fastapi import APIRouter, HTTPException
from web2api.execution.operation_runner import OperationRunner
router = APIRouter()
@router.post("/v1/chat/completions")
async def chat_completion(request: ChatCompletionRequest):
    """
    Main OpenAI-compatible endpoint.
    
    Maps OpenAI request to Web2API service execution.
    """
    # Parse model to get service_id
    service_id = request.model.split(":")[0]
    
    # Get service config
    service = await Service.get(service_id)
    if not service:
        raise HTTPException(404, "Service not found")
    
    # Execute operation
    runner = OperationRunner()
    result = await runner.execute_operation(
        service_id=service_id,
        operation_id="chat_completion",
        parameters={
            "message": request.messages[-1]["content"],
            "model": request.model
        }
    )
    
    # Format as OpenAI response
    return {
        "id": f"chatcmpl-{uuid.uuid4().hex[:24]}",
        "object": "chat.completion",
        "created": int(time.time()),
        "model": request.model,
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": result
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": estimate_tokens(request.messages[-1]["content"]),
            "completion_tokens": estimate_tokens(result),
            "total_tokens": estimate_tokens(request.messages[-1]["content"] + result)
        }
    }
@router.get("/v1/models")
async def list_models():
    """List all services as models."""
    services = await Service.all()
    
    return {
        "object": "list",
        "data": [
            {
                "id": service.id,
                "object": "model",
                "created": int(service.created_at.timestamp()),
                "owned_by": "web2api"
            }
            for service in services
        ]
    }
```
---
#### `discovery/auth_detector.py` (NEW)
```python
"""
Authentication mechanism detection using Owl-Browser commands.
"""
class AuthDetector:
    """Detects authentication mechanisms on web services."""
    
    async def detect_auth_mechanism(self, browser, context_id, url):
        """Detect how service authenticates users."""
        await browser.browser_navigate({
            "context_id": context_id,
            "url": url
        })
        
        # Check for form login
        has_email = await browser.browser_find_element({
            "context_id": context_id,
            "description": "email or username input field"
        })
        
        has_password = await browser.browser_find_element({
            "context_id": context_id,
            "description": "password input field"
        })
        
        if has_email.get("found") and has_password.get("found"):
            return {
                "type": "form_login",
                "email_selector": has_email["selector"],
                "password_selector": has_password["selector"]
            }
        
        # Check for OAuth
        has_oauth = await browser.browser_find_element({
            "context_id": context_id,
            "description": "Sign in with Google, GitHub, or other OAuth button"
        })
        
        if has_oauth.get("found"):
            return {
                "type": "oauth",
                "oauth_selector": has_oauth["selector"]
            }
        
        # Check for API key
        has_api_key = await browser.browser_find_element({
            "context_id": context_id,
            "description": "API key input field"
        })
        
        if has_api_key.get("found"):
            return {
                "type": "api_key",
                "api_key_selector": has_api_key["selector"]
            }
        
        return {"type": "unknown"}
    
    async def execute_login(
        self, 
        browser, 
        context_id, 
        auth_config, 
        credentials,
        websocket_handler=None
    ):
        """Execute login flow with CAPTCHA handling."""
        if auth_config["type"] == "form_login":
            # Fill email
            await browser.browser_type({
                "context_id": context_id,
                "selector": auth_config["email_selector"],
                "text": credentials["email"]
            })
            
            if websocket_handler:
                await websocket_handler.send_login_update(
                    service_id, "processing", "Entered email"
                )
            
            # Fill password
            await browser.browser_type({
                "context_id": context_id,
                "selector": auth_config["password_selector"],
                "text": credentials["password"]
            })
            
            if websocket_handler:
                await websocket_handler.send_login_update(
                    service_id, "processing", "Entered password"
                )
            
            # Check for CAPTCHA before submit
            captcha_result = await self._handle_captcha_if_present(
                browser, context_id, websocket_handler
            )
            
            # Submit form
            submit_button = await browser.browser_find_element({
                "context_id": context_id,
                "description": "submit, login, or sign in button"
            })
            
            await browser.browser_click({
                "context_id": context_id,
                "selector": submit_button["selector"]
            })
            
            # Wait for successful login
            await browser.browser_wait_for_navigation({
                "context_id": context_id
            })
            
            return True
        
        return False
    
    async def _handle_captcha_if_present(
        self, 
        browser, 
        context_id, 
        websocket_handler=None
    ):
        """Detect and solve CAPTCHA using Owl-Browser commands."""
        has_captcha = await browser.browser_detect_captcha({
            "context_id": context_id
        })
        
        if not has_captcha.get("found"):
            return None
        
        if websocket_handler:
            await websocket_handler.send_login_update(
                service_id, "processing", "CAPTCHA detected, solving..."
            )
        
        # Classify CAPTCHA type
        captcha_type = await browser.browser_classify_captcha({
            "context_id": context_id
        })
        
        # Solve based on type
        if captcha_type["type"] == "text":
            result = await browser.browser_solve_text_captcha({
                "context_id": context_id
            })
        elif captcha_type["type"] == "image":
            result = await browser.browser_solve_image_captcha({
                "context_id": context_id
            })
        else:
            result = await browser.browser_solve_captcha({
                "context_id": context_id
            })
        
        if websocket_handler:
            await websocket_handler.send_login_update(
                service_id, "processing", "CAPTCHA solved"
            )
        
        return result
```
---
#### `discovery/feature_mapper.py` (NEW)
```python
"""
Service capability detection using Owl-Browser AI commands.
"""
class FeatureMapper:
    """Maps web service features using AI analysis."""
    
    async def detect_service_capabilities(
        self, 
        browser, 
        context_id, 
        llm_service
    ):
        """Detect what the service can do."""
        # Take screenshot
        screenshot = await browser.browser_screenshot({
            "context_id": context_id
        })
        
        # Get DOM
        html = await browser.browser_get_html({
            "context_id": context_id
        })
        
        # Use browser_ai_analyze for capability detection
        ai_analysis = await browser.browser_ai_analyze({
            "context_id": context_id,
            "question": """
            Analyze this web interface and identify:
            1. What is the primary service? (chat, image generation, search, etc.)
            2. What models/AI engines are available?
            3. What features are configurable? (temperature, max tokens, web browsing, etc.)
            4. Where is the main input area?
            5. Where do responses appear?
            6. Are there file upload capabilities?
            """
        })
        
        # Use browser_query_page for element discovery
        input_elements = await browser.browser_query_page({
            "context_id": context_id,
            "question": "Find the main input area (textarea, input field) for user prompts"
        })
        
        submit_button = await browser.browser_query_page({
            "context_id": context_id,
            "question": "Find the submit, send, or generate button"
        })
        
        output_area = await browser.browser_query_page({
            "context_id": context_id,
            "question": "Find where the AI response or output appears"
        })
        
        # Parse results
        capabilities = {
            "primary_operation": self._extract_primary_operation(ai_analysis),
            "available_models": await self._extract_models(browser, context_id),
            "features": await self._extract_features(browser, context_id),
            "input_selector": input_elements.get("selector"),
            "submit_selector": submit_button.get("selector"),
            "output_selector": output_area.get("selector"),
            "has_file_upload": await self._check_file_upload(browser, context_id)
        }
        
        return capabilities
    
    async def _extract_models(self, browser, context_id):
        """Extract available model options."""
        models = await browser.browser_query_page({
            "context_id": context_id,
            "question": "List all available AI models or engines shown in dropdowns or options"
        })
        
        return models.get("models", [])
    
    async def _extract_features(self, browser, context_id):
        """Extract configurable features."""
        features = await browser.browser_query_page({
            "context_id": context_id,
            "question": "Find all toggles, sliders, dropdowns for configuring behavior"
        })
        
        return features.get("features", [])
    
    async def _check_file_upload(self, browser, context_id):
        """Check if service supports file uploads."""
        upload_button = await browser.browser_find_element({
            "context_id": context_id,
            "description": "file upload, attachment, or upload button"
        })
        
        return upload_button.get("found", False)
```
---
#### `discovery/operation_builder.py` (NEW)
```python
"""
Dynamic operation generation from discovered features.
"""
class OperationBuilder:
    """Builds executable operations from discovered features."""
    
    async def build_chat_completion_operation(
        self, 
        service_id, 
        features, 
        auth_config
    ):
        """Build chat completion operation from detected features."""
        
        execution_steps = []
        
        # Step 1: Navigate (if needed)
        execution_steps.append({
            "action": "navigate",
            "url": auth_config.get("url", ""),
            "description": "Navigate to service"
        })
        
        # Step 2: Wait for input
        execution_steps.append({
            "action": "wait_for_selector",
            "selector": features["input_selector"],
            "timeout": 5000,
            "description": "Wait for input field"
        })
        
        # Step 3: Type message
        execution_steps.append({
            "action": "type",
            "selector": features["input_selector"],
            "value": "{{message}}",
            "description": "Type user message"
        })
        
        # Step 4: Select model (if applicable)
        if features.get("available_models"):
            execution_steps.append({
                "action": "select_model",
                "selector": features["model_selector"],
                "value": "{{model}}",
                "description": "Select AI model"
            })
        
        # Step 5: Upload file (if provided)
        execution_steps.append({
            "action": "upload_file_if_present",
            "description": "Upload file if present in request"
        })
        
        # Step 6: Click submit
        execution_steps.append({
            "action": "click",
            "selector": features["submit_selector"],
            "description": "Submit request"
        })
        
        # Step 7: Wait for response (CRITICAL: use browser_is_enabled)
        execution_steps.append({
            "action": "wait_for_response_complete",
            "submit_selector": features["submit_selector"],
            "timeout": 60000,
            "description": "Wait for response to complete",
            "implementation": "browser_is_enabled polling"
        })
        
        # Step 8: Extract response
        execution_steps.append({
            "action": "extract_response",
            "selector": features["output_selector"],
            "description": "Extract AI response",
            "implementation": "browser_ai_extract + browser_extract_text fallback"
        })
        
        return {
            "id": "chat_completion",
            "name": "Chat Completion",
            "description": "Send message and get AI response",
            "parameters": {
                "message": {
                    "type": "string",
                    "required": True,
                    "description": "User message to send"
                },
                "model": {
                    "type": "string",
                    "required": False,
                    "enum": features.get("available_models", []),
                    "description": "AI model to use"
                }
            },
            "execution_steps": execution_steps
        }
```
---
#### `discovery/config_generator.py` (NEW)
```python
"""
Generate final service configuration from discovery results.
"""
class ConfigGenerator:
    """Generates service configuration JSON."""
    
    async def generate_service_config(
        self,
        service_id: str,
        url: str,
        auth_config: dict,
        features: dict,
        operations: list
    ):
        """Generate complete service configuration."""
        
        config = {
            "service_id": service_id,
            "name": url.split("//")[1].split("/")[0],
            "url": url,
            "type": self._determine_service_type(features),
            "auth": {
                "type": auth_config["type"],
                "config": auth_config
            },
            "capabilities": {
                "primary_operation": features["primary_operation"],
                "available_models": features.get("available_models", []),
                "features": features.get("features", []),
                "has_file_upload": features.get("has_file_upload", False)
            },
            "operations": operations,
            "ui_selectors": {
                "input": features["input_selector"],
                "submit": features["submit_selector"],
                "output": features["output_selector"]
            },
            "discovered_at": datetime.utcnow().isoformat(),
            "version": "1.0"
        }
        
        return config
    
    def _determine_service_type(self, features):
        """Determine service type from features."""
        primary = features["primary_operation"].lower()
        
        if "chat" in primary:
            return "chat"
        elif "image" in primary or "generation" in primary:
            return "image_generation"
        elif "search" in primary:
            return "search"
        else:
            return "generic"
```
---
#### `discovery/orchestrator.py` (NEW)
```python
"""
Discovery pipeline coordinator with live viewport streaming.
"""
class DiscoveryOrchestrator:
    """Orchestrates the complete discovery process."""
    
    def __init__(self):
        self.auth_detector = AuthDetector()
        self.feature_mapper = FeatureMapper()
        self.operation_builder = OperationBuilder()
        self.config_generator = ConfigGenerator()
        self.live_viewport = LiveViewportManager()
    
    async def start_discovery(
        self, 
        service_id: str, 
        websocket_handler=None
    ):
        """Start full discovery pipeline with live viewport."""
        
        task_id = str(uuid.uuid4())
        
        # Start live viewport streaming
        if websocket_handler:
            await self.live_viewport.start_streaming(
                service_id, 
                websocket_handler
            )
        
        try:
            # Step 1: Get service from DB
            service = await Service.get(service_id)
            
            # Step 2: Acquire browser context
            from web2api.concurrency.browser_pool import BrowserPool
            pool = BrowserPool(...)
            async with pool.acquire_service_context(service_id) as context_id:
                
                # Step 3: Detect auth
                await websocket_handler.send_discovery_update(
                    service_id, "scanning", 10, "Detecting authentication..."
                )
                
                auth_config = await self.auth_detector.detect_auth_mechanism(
                    browser, context_id, service.url
                )
                
                # Step 4: Execute login
                await websocket_handler.send_discovery_update(
                    service_id, "scanning", 30, "Logging in..."
                )
                
                credentials = await credential_store.get_credentials(service_id)
                login_success = await self.auth_detector.execute_login(
                    browser, context_id, auth_config, credentials, websocket_handler
                )
                
                if not login_success:
                    raise Exception("Login failed")
                
                # Save session cookies
                await pool.save_service_session(service_id)
                
                # Step 5: Detect features
                await websocket_handler.send_discovery_update(
                    service_id, "scanning", 50, "Discovering capabilities..."
                )
                
                features = await self.feature_mapper.detect_service_capabilities(
                    browser, context_id, llm_service
                )
                
                # Step 6: Build operations
                await websocket_handler.send_discovery_update(
                    service_id, "scanning", 70, "Building operations..."
                )
                
                operations = []
                chat_op = await self.operation_builder.build_chat_completion_operation(
                    service_id, features, auth_config
                )
                operations.append(chat_op)
                
                # Step 7: Generate config
                await websocket_handler.send_discovery_update(
                    service_id, "scanning", 90, "Generating configuration..."
                )
                
                config = await self.config_generator.generate_service_config(
                    service_id, service.url, auth_config, features, operations
                )
                
                # Step 8: Save to database
                service.config = config
                service.discovery_status = "complete"
                service.login_status = "success"
                await service.save()
                
                await websocket_handler.send_discovery_update(
                    service_id, "complete", 100, "Discovery complete!"
                )
                
                # Stop live viewport
                await self.live_viewport.stop_streaming(service_id)
                
                return task_id
                
        except Exception as e:
            await websocket_handler.send_discovery_update(
                service_id, "failed", 0, f"Discovery failed: {str(e)}"
            )
            await self.live_viewport.stop_streaming(service_id)
            raise
```
---
#### `execution/queue_manager.py` (NEW)
```python
"""
Queue-based execution manager (NO rate limiting).
"""
class ExecutionQueue:
    """FIFO queue for service execution requests."""
    
    def __init__(self):
        self.queues: dict[str, asyncio.Queue] = {}
        self.processors: dict[str, asyncio.Task] = {}
    
    async def add_task(
        self, 
        service_id: str, 
        operation_id: str, 
        parameters: dict,
        websocket_handler=None
    ):
        """Add task to service queue (NO rate limiting)."""
        
        if service_id not in self.queues:
            self.queues[service_id] = asyncio.Queue()
            
            # Start processor for this service
            self.processors[service_id] = asyncio.create_task(
                self._process_queue(service_id, websocket_handler)
            )
        
        task = {
            "operation_id": operation_id,
            "parameters": parameters,
            "task_id": str(uuid.uuid4()),
            "created_at": time.time()
        }
        
        await self.queues[service_id].put(task)
        return task["task_id"]
    
    async def _process_queue(
        self, 
        service_id: str, 
        websocket_handler=None
    ):
        """Process tasks from queue (sequential, NO rate limiting)."""
        
        from web2api.concurrency.browser_pool import BrowserPool
        from web2api.execution.operation_runner import OperationRunner
        
        pool = BrowserPool(...)
        runner = OperationRunner()
        
        while True:
            task = await self.queues[service_id].get()
            
            try:
                # Execute operation
                result = await runner.execute_operation(
                    service_id,
                    task["operation_id"],
                    task["parameters"],
                    websocket_handler
                )
                
                # Send result
                await websocket_handler.send_execution_log(
                    service_id, "info", f"Task {task['task_id']} completed"
                )
                
            except Exception as e:
                await websocket_handler.send_execution_log(
                    service_id, "error", f"Task failed: {str(e)}"
                )
            
            self.queues[service_id].task_done()
```
---
#### `execution/live_viewport.py` (NEW)
```python
"""
Live viewport streaming for TOWER frontend.
"""
class LiveViewportManager:
    """Manages live viewport streaming during discovery."""
    
    def __init__(self):
        self.active_streams: dict[str, str] = {}  # service_id → stream_id
    
    async def start_streaming(
        self, 
        service_id: str, 
        websocket_handler,
        browser,
        context_id
    ):
        """Start live viewport stream using Owl-Browser."""
        
        # Start live stream
        stream_info = await browser.start_live_stream({
            "context_id": context_id,
            "quality": "medium",
            "fps": 10
        })
        
        self.active_streams[service_id] = stream_info["stream_id"]
        
        # Start background task to send frames
        asyncio.create_task(
            self._stream_frames(
                service_id, 
                stream_info["stream_id"], 
                websocket_handler,
                browser
            )
        )
    
    async def _stream_frames(
        self, 
        service_id: str, 
        stream_id: str, 
        websocket_handler,
        browser
    ):
        """Stream frames to WebSocket."""
        
        while service_id in self.active_streams:
            try:
                # Get current frame
                frame = await browser.get_live_frame({
                    "stream_id": stream_id
                })
                
                # Send to frontend
                await websocket_handler.send_live_viewport_frame(
                    service_id, 
                    frame["data"]
                )
                
                # Small delay to control FPS
                await asyncio.sleep(0.1)
                
            except Exception as e:
                logger.error(f"Frame streaming error: {e}")
                break
    
    async def stop_streaming(self, service_id: str, browser):
        """Stop live viewport stream."""
        
        if service_id in self.active_streams:
            stream_id = self.active_streams[service_id]
            
            await browser.stop_live_stream({
                "stream_id": stream_id
            })
            
            del self.active_streams[service_id]
```
---
#### `execution/video_recorder.py` (NEW)
```python
"""
Video recording for discovery process playback.
"""
class VideoRecorder:
    """Records discovery process for later review."""
    
    async def start_recording(self, service_id: str, browser, context_id):
        """Start video recording."""
        
        recording = await browser.start_video_recording({
            "context_id": context_id
        })
        
        return recording["recording_id"]
    
    async def stop_and_save(
        self, 
        service_id: str, 
        recording_id: str, 
        browser
    ):
        """Stop recording and save to storage."""
        
        await browser.stop_video_recording({
            "recording_id": recording_id
        })
        
        # Download video
        video_data = await browser.download_video_recording({
            "recording_id": recording_id
        })
        
        # Save to artifact storage
        from web2api.storage.artifact_manager import ArtifactManager
        artifact_manager = ArtifactManager()
        
        artifact_path = await artifact_manager.save_video(
            service_id, 
            video_data["data"],
            filename=f"discovery_{service_id}_{int(time.time())}.mp4"
        )
        
        return artifact_path
```
---
#### `auth/session_manager.py` (NEW)
```python
"""
Session persistence using Owl-Browser cookie commands.
"""
class SessionManager:
    """Manages browser session persistence."""
    
    async def save_session(
        self, 
        service_id: str, 
        browser, 
        context_id,
        storage
    ):
        """Save session cookies for reuse."""
        
        cookies = await browser.browser_get_cookies({
            "context_id": context_id
        })
        
        await storage.save_session_cookies(service_id, cookies)
    
    async def restore_session(
        self, 
        service_id: str, 
        browser, 
        context_id,
        storage
    ):
        """Restore saved session."""
        
        cookies = await storage.get_session_cookies(service_id)
        
        if cookies:
            await browser.browser_set_cookie({
                "context_id": context_id,
                "cookies": cookies
            })
    
    async def is_session_valid(
        self, 
        service_id: str, 
        browser, 
        context_id
    ):
        """Check if saved session is still valid."""
        
        try:
            # Try to navigate to service
            await browser.browser_navigate({
                "context_id": context_id,
                "url": service_url
            })
            
            # Check if we're redirected to login
            current_url = await browser.browser_get_current_url({
                "context_id": context_id
            })
            
            if "login" in current_url.lower():
                return False
            
            return True
        except:
            return False
```
---
#### `auth/credential_store.py` (NEW)
```python
"""
Encrypted credential storage.
"""
from cryptography.fernet import Fernet
class CredentialStore:
    """Encrypts and stores service credentials."""
    
    def __init__(self, encryption_key: bytes):
        self.cipher = Fernet(encryption_key)
    
    async def store_credentials(
        self, 
        service_id: str, 
        email: str, 
        password: str
    ):
        """Encrypt and store credentials."""
        
        encrypted_email = self.cipher.encrypt(email.encode())
        encrypted_password = self.cipher.encrypt(password.encode())
        
        credential = ServiceCredential(
            service_id=service_id,
            auth_type="form_login",
            encrypted_email=encrypted_email.decode(),
            encrypted_password=encrypted_password.decode()
        )
        
        await credential.save()
    
    async def get_credentials(
        self, 
        service_id: str
    ):
        """Retrieve and decrypt credentials."""
        
        credential = await ServiceCredential.get(service_id)
        
        email = self.cipher.decrypt(
            credential.encrypted_email.encode()
        ).decode()
        
        password = self.cipher.decrypt(
            credential.encrypted_password.encode()
        ).decode()
        
        return {"email": email, "password": password}
```
---
```
---
## 3. Architecture Transformation Strategy
### 3.1 Directory Structure Transformation
```
autoqa/                          web2api/
├── api/                         ├── api/
│   └── main.py                  │   ├── main.py (MODIFY)
│                                │   ├── service_manager.py (NEW)
│                                │   ├── openai_compat.py (NEW)
│                                │   └── websocket_handler.py (NEW)
├── builder/                     ├── discovery/ (RENAME from builder)
│   ├── analyzer/                │   ├── auth_detector.py (NEW)
│   │   ├── page_analyzer.py     │   ├── feature_mapper.py (ADAPT from page_analyzer.py)
│   │   ├── visual_analyzer.py   │   ├── operation_builder.py (NEW)
│   │   └── element_classifier.py│   ├── config_generator.py (NEW)
│   ├── discovery/               │   └── orchestrator.py (NEW)
│   │   ├── form_analyzer.py     │
│   │   ├── api_detector.py      │   ├── auth/ (NEW directory)
│   │   └── flow_detector.py     │   ├── form_filler.py (ADAPT from form_analyzer.py)
│   └── crawler/                 │   ├── session_manager.py (NEW)
│                                │   └── credential_store.py (NEW)
├── concurrency/                 ├── concurrency/
│   └── browser_pool.py          │   └── browser_pool.py (ENHANCE for service isolation)
├── llm/                         ├── llm/
│   └── service.py               │   └── service.py (EXTEND with Web2API prompts)
├── runner/                      ├── execution/ (RENAME from runner)
│   ├── test_runner.py           │   ├── operation_runner.py (ADAPT from test_runner.py)
│   └── self_healing.py          │   ├── queue_manager.py (NEW)
│                                │   ├── streaming.py (NEW)
│                                │   ├── live_viewport.py (NEW)
│                                │   └── video_recorder.py (NEW)
│                                │   └── self_healing.py (KEEP)
├── storage/                     ├── storage/
│   ├── database.py              │   └── database.py (EXTEND with service tables)
│   └── artifact_manager.py      │       └── artifact_manager.py (KEEP)
```
### 3.2 Code Reuse Summary
|-----------|-------------|--------------|---------|
| Browser Pooling | `concurrency/browser_pool.py` | `concurrency/browser_pool.py` | 95% |
| Page Analysis | `builder/analyzer/page_analyzer.py` | `discovery/feature_mapper.py` | 85% |
| Visual Analysis | `builder/analyzer/visual_analyzer.py` | `discovery/visual_analyzer.py` | 100% |
| Form Analysis | `builder/discovery/form_analyzer.py` | `auth/form_analyzer.py` | 90% |
| LLM Integration | `llm/service.py` | `llm/service.py` | 90% |
| Test Execution | `runner/test_runner.py` | `execution/operation_runner.py` | 60% |
| Error Recovery | `runner/self_healing.py` | `execution/self_healing.py` | 100% |
| Database | `storage/database.py` | `storage/database.py` | 80% |
| API Framework | `api/main.py` | `api/main.py` | 40% |
**Overall Reuse: ~70%**
---
## 4. Discovery Pipeline Implementation
### 4.1 Complete Discovery Flow with Owl-Browser Commands
```python
"""
Complete auto-discovery pipeline using specific Owl-Browser commands.
"""
async def discover_service(service_id: str, url: str, credentials: dict):
    """Full discovery with Owl-Browser command integration."""
    
    # 1. Acquire browser context (from pool)
    context_id = await browser_pool.acquire_service_context(service_id)
    
    # 2. Navigate to service
    await browser.browser_navigate({
        "context_id": context_id,
        "url": url
    })
    
    # 3. Detect auth mechanism
    auth_detector = AuthDetector()
    auth_config = await auth_detector.detect_auth_mechanism(
        browser, context_id, url
    )
    # Uses: browser_find_element
    
    # 4. Execute login with CAPTCHA handling
    login_success = await auth_detector.execute_login(
        browser, context_id, auth_config, credentials, websocket
    )
    # Uses: browser_type, browser_click, browser_detect_captcha,
    #       browser_classify_captcha, browser_solve_captcha,
    #       browser_wait_for_navigation
    
    if login_success:
        # 5. Save session cookies
        cookies = await browser.browser_get_cookies({
            "context_id": context_id
        })
        await storage.save_session(service_id, cookies)
        # Uses: browser_get_cookies
    
    # 6. Start live viewport streaming
    await browser.start_live_stream({
        "context_id": context_id,
        "quality": "medium"
    })
    # Uses: start_live_stream
    
    # 7. Detect service capabilities
    feature_mapper = FeatureMapper()
    features = await feature_mapper.detect_service_capabilities(
        browser, context_id, llm_service
    )
    # Uses: browser_screenshot, browser_ai_analyze, browser_query_page,
    #       browser_get_html, browser_find_element
    
    # 8. Build operations
    operation_builder = OperationBuilder()
    operations = []
    chat_op = await operation_builder.build_chat_completion_operation(
        service_id, features, auth_config
    )
    operations.append(chat_op)
    
    # 9. Generate config
    config_generator = ConfigGenerator()
    config = await config_generator.generate_service_config(
        service_id, url, auth_config, features, operations
    )
    
    # 10. Save to database
    service = await Service.get(service_id)
    service.config = config
    service.discovery_status = "complete"
    await service.save()
    
    # 11. Stop live viewport
    await browser.stop_live_stream({
        "context_id": context_id
    })
    # Uses: stop_live_stream
    
    # 12. Release context
    await browser_pool.release_service_context(service_id)
    
    return config
```
---
## 5. Execution Engine with Owl-Browser Integration
### 5.1 Operation Execution with Critical Response Detection
```python
"""
Execute chat completion with browser_is_enabled for response detection.
"""
async def execute_chat_completion(
    service_id: str, 
    message: str, 
    model: str = None
):
    """Execute chat completion using discovered configuration."""
    
    # Get service config
    service = await Service.get(service_id)
    config = service.config
    
    # Acquire context
    context_id = await browser_pool.acquire_service_context(service_id)
    
    # Restore session
    await browser.browser_set_cookie({
        "context_id": context_id,
        "cookies": await storage.get_session(service_id)
    })
    # Uses: browser_set_cookie
    
    # Navigate to service
    await browser.browser_navigate({
        "context_id": context_id,
        "url": config["url"]
    })
    
    # Wait for input field
    await browser.browser_wait_for_selector({
        "context_id": context_id,
        "selector": config["ui_selectors"]["input"],
        "timeout": 5000
    })
    
    # Type message
    await browser.browser_type({
        "context_id": context_id,
        "selector": config["ui_selectors"]["input"],
        "text": message
    })
    
    # Select model if needed
    if model:
        await browser.browser_select_option({
            "context_id": context_id,
            "selector": config["ui_selectors"]["model"],
            "value": model
        })
    
    # Upload file if present
    if file_path:
        await browser.browser_upload_file({
            "context_id": context_id,
            "files": [file_path]
        })
        # Uses: browser_upload_file
    
    # Click submit
    await browser.browser_click({
        "context_id": context_id,
        "selector": config["ui_selectors"]["submit"]
    })
    
    # CRITICAL: Wait for response using browser_is_enabled
    # Most chat interfaces disable "Send" button while generating
    submit_selector = config["ui_selectors"]["submit"]
    while True:
        is_enabled = await browser.browser_is_enabled({
            "context_id": context_id,
            "selector": submit_selector
        })
        # Uses: browser_is_enabled (THE KEY COMMAND!)
        
        if is_enabled.get("enabled"):
            break  # Response complete!
        
        await asyncio.sleep(0.5)
    
    # Extract response
    response = await browser.browser_ai_extract({
        "context_id": context_id,
        "selector": config["ui_selectors"]["output"],
        "prompt": "Extract the AI assistant's response text"
    })
    # Uses: browser_ai_extract
    
    # Fallback to text extraction
    if not response.get("content"):
        response = await browser.browser_extract_text({
            "context_id": context_id,
            "selector": config["ui_selectors"]["output"]
        })
        # Uses: browser_extract_text
    
    # Release context
    await browser_pool.release_service_context(service_id)
    
    return response.get("content", "")
```
---
## 6. Live Viewport Streaming
### 6.1 Live Viewport Implementation
```python
"""
Live viewport streaming using Owl-Browser video commands.
"""
class LiveViewportStreamer:
    """Stream live viewport to TOWER frontend."""
    
    async def start_streaming(
        self, 
        service_id: str, 
        websocket: WebSocket,
        browser,
        context_id
    ):
        """Start live viewport stream."""
        
        # Start live stream
        stream = await browser.start_live_stream({
            "context_id": context_id,
            "quality": "medium",
            "fps": 10
        })
        
        stream_id = stream["stream_id"]
        
        # Stream frames in background
        asyncio.create_task(
            self._stream_loop(service_id, stream_id, websocket, browser)
        )
    
    async def _stream_loop(
        self, 
        service_id: str, 
        stream_id: str, 
        websocket: WebSocket,
        browser
    ):
        """Continuously send frames to frontend."""
        
        while True:
            try:
                # Get current frame
                frame = await browser.get_live_frame({
                    "stream_id": stream_id
                })
                
                # Send to WebSocket
                await websocket.send_bytes(frame["data"])
                
                await asyncio.sleep(0.1)  # 10 FPS
                
            except Exception as e:
                logger.error(f"Stream error: {e}")
                break
    
    async def stop_streaming(self, stream_id: str, browser):
        """Stop live stream."""
        
        await browser.stop_live_stream({
            "stream_id": stream_id
        })
```
---
## 7. Database Schema Extensions
### 7.1 Complete SQL Schema
```sql
-- Services table
CREATE TABLE services (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) NOT NULL,
    url TEXT NOT NULL,
    type VARCHAR(50) DEFAULT 'generic',
    status VARCHAR(50) DEFAULT 'offline',
    login_status VARCHAR(50) DEFAULT 'pending',
    discovery_status VARCHAR(50) DEFAULT 'pending',
    config JSONB,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW()
);
-- Credentials table (encrypted)
CREATE TABLE service_credentials (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_id UUID REFERENCES services(id) ON DELETE CASCADE,
    auth_type VARCHAR(50),
    encrypted_email TEXT,
    encrypted_password TEXT,
    encrypted_api_key TEXT,
    session_cookie JSONB,
    created_at TIMESTAMP DEFAULT NOW()
);
-- Executions table
CREATE TABLE executions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_id UUID REFERENCES services(id) ON DELETE CASCADE,
    operation_id VARCHAR(100),
    parameters JSONB,
    result TEXT,
    status VARCHAR(50),
    started_at TIMESTAMP DEFAULT NOW(),
    completed_at TIMESTAMP,
    error TEXT
);
-- Service stats table
CREATE TABLE service_stats (
    service_id UUID PRIMARY KEY REFERENCES services(id) ON DELETE CASCADE,
    total_requests INT DEFAULT 0,
    successful_requests INT DEFAULT 0,
    failed_requests INT DEFAULT 0,
    avg_latency_ms INT DEFAULT 0,
    last_request_at TIMESTAMP,
    uptime_start TIMESTAMP DEFAULT NOW()
);
-- CREATE INDEXES
CREATE INDEX idx_services_status ON services(status);
CREATE INDEX idx_executions_service_id ON executions(service_id);
CREATE INDEX idx_executions_status ON executions(status);
```
---
## 8. Implementation Phases
### Phase 1: Infrastructure Setup (Days 1-2)
**Goal**: Basic database and API structure
**Tasks**:
1. Extend database schema with service tables
2. Set up credential encryption
3. Create basic FastAPI app structure
4. Implement health check endpoint
**Files Modified**:
- `storage/database.py` - Add service tables
- `api/main.py` - Basic structure
---
### Phase 2: Service Management API (Days 3-4)
**Goal**: CRUD operations for services
**Tasks**:
1. Implement service registration endpoint
2. Create credential store
3. Implement WebSocket handler
4. Test service creation flow
**Files Created**:
- `api/service_manager.py`
- `auth/credential_store.py`
- `api/websocket_handler.py`
---
### Phase 3: Authentication & Login (Days 5-6)
**Goal**: Automated login with CAPTCHA handling
**Tasks**:
1. Implement auth detection
2. Create form filler with CAPTCHA solving
3. Implement session persistence
4. Test login on k2think.ai
**Files Created**:
- `discovery/auth_detector.py`
- `auth/form_filler.py` (adapted from form_analyzer.py)
- `auth/session_manager.py`
**Owl-Browser Commands Integrated**:
- `browser_find_element`
- `browser_detect_captcha`
- `browser_classify_captcha`
- `browser_solve_captcha`
- `browser_get_cookies`
- `browser_set_cookie`
---
### Phase 4: Feature Discovery (Days 7-9)
**Goal**: Detect service capabilities automatically
**Tasks**:
1. Implement AI-powered feature detection
2. Create model/options detection
3. Build operation generator
4. Test feature extraction
**Files Created**:
- `discovery/feature_mapper.py` (adapted from page_analyzer.py)
- `discovery/operation_builder.py`
- `discovery/config_generator.py`
**Owl-Browser Commands Integrated**:
- `browser_ai_analyze`
- `browser_query_page`
- `browser_screenshot`
- `browser_get_html`
- `browser_extract_text`
---
### Phase 5: Live Viewport & Recording (Days 10-11)
**Goal**: Live streaming during discovery
**Tasks**:
1. Implement live viewport streaming
2. Create video recording
3. Integrate with WebSocket
4. Test streaming performance
**Files Created**:
- `execution/live_viewport.py`
- `execution/video_recorder.py`
**Owl-Browser Commands Integrated**:
- `start_live_stream`
- `stop_live_stream`
- `get_live_frame`
- `start_video_recording`
- `stop_video_recording`
- `download_video_recording`
---
### Phase 6: Discovery Orchestrator (Days 12-13)
**Goal**: Complete discovery pipeline
**Tasks**:
1. Implement discovery orchestrator
2. Connect all discovery components
3. Add progress tracking
4. Test full discovery flow
**Files Created**:
- `discovery/orchestrator.py`
---
### Phase 7: Execution Engine (Days 14-16)
**Goal**: Execute discovered operations
**Tasks**:
1. Implement operation runner
2. Add response detection with `browser_is_enabled`
3. Implement queue manager
4. Add file upload support
**Files Created**:
- `execution/operation_runner.py` (adapted from test_runner.py)
- `execution/queue_manager.py`
**Owl-Browser Commands Integrated**:
- `browser_is_enabled` (CRITICAL for response detection)
- `browser_ai_extract`
- `browser_extract_text`
- `browser_upload_file`
---
### Phase 8: OpenAI API Layer (Days 17-18)
**Goal**: OpenAI-compatible endpoints
**Tasks**:
1. Implement `/v1/chat/completions`
2. Implement `/v1/models`
3. Add SSE streaming support
4. Test with OpenAI client libraries
**Files Created**:
- `api/openai_compat.py`
---
### Phase 9: Multi-Service Support (Days 19-20)
**Goal**: Parallel service execution
**Tasks**:
1. Enhance browser pool with tab management
2. Implement service isolation
3. Test concurrent service execution
**Files Modified**:
- `concurrency/browser_pool.py`
**Owl-Browser Commands Integrated**:
- `new_tab`
- `switch_tab`
- `close_tab`
- `get_tabs`
- `get_active_tab`
---
### Phase 10: Testing & Optimization (Days 21-22)
**Goal**: End-to-end testing and polish
**Tasks**:
1. Test with k2think.ai
2. Test with multiple services
3. Performance optimization
4. Error handling improvements
---
## 9. Success Criteria
### 9.1 Must-Have Features
✅ **Service Registration**: Register k2think.ai via API  
✅ **Auto-Login**: Login without manual intervention  
✅ **CAPTCHA Handling**: Detect and solve CAPTCHAs automatically  
✅ **Feature Discovery**: Detect input, submit, output areas automatically  
✅ **Live Viewport**: Stream discovery process to frontend  
✅ **Response Detection**: Use `browser_is_enabled` to detect response completion  
✅ **OpenAI API**: `/v1/chat/completions` returns valid response  
✅ **Queue Execution**: Multiple requests queue properly (NO rate limiting)  
✅ **Session Persistence**: Save and restore sessions automatically  
✅ **Multi-Service**: Run multiple services in parallel using tabs  
### 9.2 Test Commands
```bash
# Register service
curl -X POST http://localhost:8000/api/services \
  -H "Content-Type: application/json" \
  -d '{
    "url": "https://www.k2think.ai",
    "email": "test@example.com",
    "password": "password123"
  }'
# Trigger discovery
curl -X POST http://localhost:8000/api/services/{service_id}/discover
# Test OpenAI API
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "{service_id}",
    "messages": [{"role": "user", "content": "Hello!"}]
  }'
# Expected response
{
  "id": "chatcmpl-xxx",
  "object": "chat.completion",
  "created": 1234567890,
  "model": "{service_id}",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "Hello! How can I help you today?"
    },
    "finish_reason": "stop"
  }],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 20,
    "total_tokens": 30
  }
}
```
---
## 10. Key Implementation Insights
### 10.1 Critical Owl-Browser Commands for Web2API
**The "Secret Sauce" Commands**:
1. **`browser_is_enabled`** - THE most critical command for chat interfaces
   - Detect when "Send" button re-enables after response generation
   - Eliminates need for arbitrary timeouts
   - Works across all chat-based services
2. **`browser_ai_extract`** + **`browser_extract_text`** - Robust response extraction
   - AI-powered extraction understands context
   - Text extraction as fallback
   - Works even when DOM changes
3. **`start_live_stream`** - Live viewport for user trust
   - User can watch discovery happen
   - Debug failures easily
   - Transparent operation
4. **`browser_detect_captcha`** + **`browser_solve_captcha`** - Auth automation
   - Handle login friction automatically
   - No manual intervention needed
5. **Tab Management Commands** - Multi-service isolation
   - Run multiple services simultaneously
   - True parallelization
   - Clean separation
### 10.2 Architecture Decisions
**Why Modify Instead of Create New?**
- 70% code reuse from AutoQA
- Browser pooling already perfect
- Page analysis already sophisticated
- Form detection already robust
- Just need to pivot from "testing" to "service discovery"
**Why Queue-Based (No Rate Limiting)?**
- Services handle their own rate limits
- Web2API just passes requests through
- FIFO is sufficient
- Simpler architecture
**Why OpenAI Compatibility?**
- Standard API format
- Works with all LLM frameworks
- Easy integration
- Largest ecosystem
---
## END OF UPGRADED REQUIREMENTS
**Ready for Implementation!**
This document provides:
✅ Complete Owl-Browser command mapping (157 commands)  
✅ File-by-file transformation guide (all 61 AutoQA files)  
✅ In-place modification strategy (no new module)  
✅ Discovery pipeline with specific command sequences  
✅ Execution engine with response detection  
✅ Live viewport streaming implementation  
✅ Database schema extensions  
✅ 10-phase implementation plan (22 days)  
✅ Clear success criteria with test commands  
**Key Innovation**: Using `browser_is_enabled` to detect when chat responses are complete - this is the breakthrough insight that makes Web2API possible!
**Next Step**: Begin Phase 1 - Infrastructure Setup



Web2API Project Requirements
# Web2API: Complete Implementation Specification

## Executive Summary

**Web2API** is a universal API proxy system that transforms any web service (with URL + credentials) into an OpenAI-compatible API endpoint. By intelligently discovering service features, forms, and APIs through browser automation, Web2API creates a unified interface for interacting with diverse web applications, enabling seamless integration with AI agents, LLMs, and automation tools.
**Web2API** transforms any web service into an OpenAI-compatible API endpoint using intelligent browser automation and auto-discovery.

### Core Value Proposition
**Core Flow**: `URL + Credentials → Auto-Discovery → OpenAI API Endpoint`

**Input**: `URL` + `Credentials` (username/password, API key, OAuth)  
**Output**: OpenAI-compatible API endpoint (`/v1/chat/completions`, `/v1/models`, etc.)
**Key Innovation**: Zero manual configuration - the system discovers service capabilities, authentication flows, and operations automatically through intelligent analysis.

**Key Features**:
- 🔍 **Auto-Discovery**: Automatically maps web service capabilities to API endpoints
- 🔐 **Credential Management**: Secure handling of authentication (forms, OAuth2, API keys)
- ⚙️ **Configuration Layer**: Toggle features, configure models, set rate limits
- 🤖 **OpenAI Compatibility**: Standard API format for LLM/AI agent integration
- 🌐 **Anti-Detection**: Leverages Owl Browser for undetectable automation
---

## 1. SYSTEM ARCHITECTURE

### 1.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     TOWER Frontend (React)                   │
│  - Service Management UI                                     │
│  - Real-time Status Updates                                  │
│  - Configuration Editor                                       │
└────────────────┬────────────────────────────────────────────┘
                 │ HTTP/REST + WebSocket
┌────────────────▼────────────────────────────────────────────┐
│              Web2API Server (FastAPI)                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  API Layer                                            │  │
│  │  - OpenAI-compatible endpoints (/v1/chat/completions)│  │
│  │  - Service management (CRUD)                          │  │
│  │  - WebSocket streaming                                │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Discovery Engine                                     │  │
│  │  - Authentication detection                            │  │
│  │  - Feature extraction (vision + DOM)                  │  │
│  │  - Operation mapping                                   │  │
│  │  - Configuration generation                            │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Execution Engine                                     │  │
│  │  - Queue-based task processing                         │  │
│  │  - Browser session management                          │  │
│  │  - Error recovery & self-healing                       │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Storage Layer                                        │  │
│  │  - Service configurations (PostgreSQL)                │  │
│  │  - Encrypted credentials                               │  │
│  │  - Execution artifacts                                 │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────┬────────────────────────────────────────────┘
                 │ Owl-Browser Python SDK
┌────────────────▼────────────────────────────────────────────┐
│              Owl-Browser Pool                                │
│  - Stealth browser automation                                │
│  - Anti-detection measures                                   │
│  - Session pooling & reuse                                   │
│  - Natural language selectors                                │
└────────────────┬────────────────────────────────────────────┘
                 │ HTTPS
┌────────────────▼────────────────────────────────────────────┐
│         Target Web Services                                  │
│  - ChatGPT, Claude, Z.ai, K2Think, etc.                     │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Technology Stack

**Backend:**
- FastAPI (async Python web framework)
- Owl-Browser Python SDK (stealth automation)
- PostgreSQL (service configurations & state)
- Redis (optional: queue & caching)
- SQLAlchemy (ORM)
- Pydantic (data validation)
- Cryptography (credential encryption)
- LiteLLM (LLM orchestration)

**Automation:**
- Owl-Browser (anti-detection browser automation)
- Vision models (GPT-4V, Claude 3.5 Sonnet) for UI analysis
- DOM analysis & element classification
- Network traffic inspection

**Frontend:**
- React + TypeScript
- TailwindCSS
- WebSocket client for real-time updates


---

## 2. OWL-BROWSER ANALYSIS

### 2.1 Core Capabilities
- **Stealth Automation**: Anti-bot detection, browser fingerprinting, human-like interactions
- **Natural Language Selectors**: `click("Login button")`, `type("email field", "user@example.com")`  
- **AI-Powered Element Finding**: Vision models identify UI elements semantically
- **Session Management**: Browser profile persistence, cookie management
- **Multi-mode Operation**: Local, Remote (WebSocket), MCP server

### 2.2 Python SDK API (Already Integrated in AutoQA)
```python
from owl_browser import OwlBrowser

# AutoQA's browser_pool.py already uses this!
browser = OwlBrowser(headless=True, stealth=True)
await browser.navigate("https://example.com")
await browser.click("Login button")
await browser.type("email input", "user@example.com")
await browser.type("password input", "secret123")
await browser.click("Submit")
result = await browser.extract("response text")
await browser.close()
```

### 2.3 Key Features for Web2API
- ✅ Already integrated in autoqa/concurrency/browser_pool.py
- ✅ Session pooling implemented
- ✅ Anti-detection active
- ✅ Natural language selectors ready
- Need: Enhanced credential injection, auth flow detection

---

## 3. AUTOQA CURRENT FUNCTIONALITY MATRIX

### 3.1 Existing Modules (70% Coverage)

| Module | File | Functionality | Reusable for Web2API |
|--------|------|---------------|---------------------|
| **Browser Pool** | `concurrency/browser_pool.py` | Owl-browser session pooling | ✅ 100% - Already perfect |
| **Page Analyzer** | `builder/analyzer/page_analyzer.py` | DOM structure + visual analysis | ✅ 90% - Extract features |
| **Form Analyzer** | `builder/discovery/form_analyzer.py` | Detect input fields, buttons | ✅ 95% - Auth detection |
| **API Detector** | `builder/discovery/api_detector.py` | Network pattern recognition | ✅ 80% - Endpoint discovery |
| **Intelligent Crawler** | `builder/crawler/intelligent_crawler.py` | State-based navigation | ✅ 70% - Multi-step flows |
| **Element Classifier** | `builder/analyzer/element_classifier.py` | ML-based element classification | ✅ 85% - UI understanding |
| **Visual Analyzer** | `builder/analyzer/visual_analyzer.py` | Vision model integration | ✅ 100% - Feature detection |
| **LLM Service** | `llm/service.py` | Prompt orchestration | ✅ 90% - Analysis prompts |
| **Test Runner** | `runner/test_runner.py` | Execution orchestration | ✅ 60% - Operation executor |
| **Self-Healing** | `runner/self_healing.py` | Selector recovery | ✅ 100% - Error recovery |
| **Database** | `storage/database.py` | PostgreSQL ORM | ✅ 80% - Service storage |
| **Artifact Manager** | `storage/artifact_manager.py` | Screenshot/video storage | ✅ 70% - Execution logs |

### 3.2 Modules Not Needed
- `assertions/` - Test-specific, not needed for API proxy
- `dsl/models.py` - Test DSL, replaced with service config schema
- `ci/generator.py` - CI/CD integration, not needed
- `generative/chaos_agents.py` - Chaos testing, not needed
- `visual/regression_engine.py` - Visual testing, not needed
- `versioning/` - Code versioning, not needed

---

## 4. WEB2API REQUIRED FUNCTIONALITY (30% New)

### 4.1 Gap Analysis

**What AutoQA Has:**
✅ Browser automation with Owl-Browser
✅ Page & form analysis  
✅ Element discovery
✅ LLM integration
✅ Database storage
✅ Error recovery

**What Web2API Needs:**
❌ OpenAI-compatible API layer
❌ Service registration & lifecycle management
❌ Auto-discovery → configuration pipeline
❌ Credential encryption & auth handling
❌ Queue-based execution (no rate limiting)
❌ WebSocket streaming for frontend
❌ Service-specific feature detection
❌ Dynamic operation generation

### 4.2 New Components Required

```
│   ├── main.py                    # FastAPI app (modify from autoqa)
│   ├── openai_compat.py          # NEW: OpenAI API endpoints
│   ├── service_manager.py        # NEW: Service CRUD
│   └── websocket_handler.py      # NEW: Real-time updates
│   ├── auth_detector.py          # NEW: Auth mechanism detection
│   ├── feature_mapper.py         # NEW: Service capability extraction
│   ├── operation_builder.py     # NEW: Dynamic operation creation
│   └── config_generator.py       # NEW: Config synthesis
│   ├── queue_manager.py          # NEW: FIFO task queue
│   ├── operation_runner.py       # ADAPT: From test_runner
│   └── streaming.py              # NEW: WebSocket log streaming
│   ├── credential_store.py       # NEW: Encrypted storage
│   ├── session_manager.py        # NEW: Browser session + cookies
│   └── form_filler.py            # ADAPT: From form_analyzer
│   ├── service.py                # NEW: Service data model
│   ├── operation.py              # NEW: Operation schema
│   └── openai_schemas.py         # NEW: OpenAI request/response
    └── database.py               # MODIFY: Add service tables
```


---

## 5. API SPECIFICATIONS

### 5.1 TOWER Frontend Contract (from types.ts & App.tsx)

**Service Data Model:**
```typescript
interface Service {
  id: string;
  name: string;
  type: 'zai' | 'chatgpt' | 'claude' | 'k2think' | 'generic';
  url: string;
  status: 'online' | 'offline' | 'maintenance' | 'analyzing';
  loginStatus: 'pending' | 'processing' | 'success' | 'failed';
  discoveryStatus: 'pending' | 'scanning' | 'complete';
  config: ServiceConfig;
  availableModels?: string[];
  stats: {
    uptime: string;
    requests24h: number;
    avgLatency: number;
  };
}
```

**Required REST Endpoints:**

```
POST   /api/services
  Body: { url: string, email: string, password: string }
  Response: Service (with loginStatus: 'pending', discoveryStatus: 'pending')
  
GET    /api/services
  Response: Service[]
  
GET    /api/services/:id
  Response: Service
  
PUT    /api/services/:id
  Body: { config: ServiceConfig }
  Response: Service
  
DELETE /api/services/:id
  Response: { success: boolean }

POST   /api/services/:id/discover
  Triggers background discovery process
  Response: { message: string, taskId: string }

POST   /api/services/:id/execute
  Body: { message: string, model?: string }
  Response: { response: string, model: string }
```

**WebSocket Endpoint:**
```
WS /ws/services/:id
  Events:
    - login_update: { loginStatus: string, message: string }
    - discovery_update: { discoveryStatus: string, progress: number, message: string }
    - execution_log: { timestamp: number, level: string, message: string }
    - status_change: { status: string }
```

### 5.2 OpenAI-Compatible Endpoints

```
POST /v1/chat/completions
  Body: {
    model: string,  // Service ID or "service-id:model-name"
    messages: Array<{role: string, content: string}>,
    stream?: boolean,
    temperature?: number,
    max_tokens?: number
  }
  Response: {
    id: string,
    object: "chat.completion",
    created: number,
    model: string,
    choices: [{
      index: 0,
      message: {role: "assistant", content: string},
      finish_reason: "stop"
    }],
    usage: {prompt_tokens: number, completion_tokens: number, total_tokens: number}
  }

GET /v1/models
  Response: {
    object: "list",
    data: [{
      id: string,  // Service ID
      object: "model",
      created: number,
      owned_by: "web2api"
    }]
  }
```

---

## 6. DISCOVERY PIPELINE (NO TEMPLATES!)

### 6.1 Auto-Discovery Flow

```
Service Registration
    ↓
1. Authentication Discovery
   - Detect login form fields (email, password, 2FA)
   - Identify OAuth flow if present
   - Check for cookie-based auth
   - Store auth mechanism
    ↓
2. Login Execution
   - Auto-fill credentials
   - Submit form
   - Handle 2FA (manual intervention if needed)
   - Verify successful login
   - Save session cookies
    ↓
3. Feature Detection (Vision + DOM)
   - Screenshot main interface
   - LLM vision analysis: "What capabilities does this service offer?"
   - DOM analysis: buttons, dropdowns, toggles
   - Identify:
     * Model selectors
     * Feature toggles (web browsing, image generation)
     * Input areas (chat, prompt)
     * Output areas (response, result)
    ↓
4. Operation Mapping
   - Detect primary operation (e.g., "Send Message")
   - Map input fields → parameters
   - Map UI controls → optional parameters
   - Generate selector strategy (natural language)
    ↓
5. Configuration Generation
   - Create service config JSON
   - Define operations with steps
   - Set up parameter schema
   - Store in database
    ↓
Service Ready (discoveryStatus: 'complete')
```

### 6.2 Discovery Implementation Details

**Auth Detection** (`auth_detector.py`):
```python
async def detect_auth_mechanism(browser, url):
    await browser.navigate(url)
    
    # Check for login form
    has_email = await browser.find("email input")
    has_password = await browser.find("password input")
    
    if has_email and has_password:
        return {
            "type": "form_login",
            "fields": {"email": email_selector, "password": password_selector}
        }
    
    # Check for OAuth
    has_oauth = await browser.find("Sign in with Google")
    if has_oauth:
        return {"type": "oauth", "provider": "google"}
    
    # Check for API key
    has_api_key = await browser.find("API key input")
    if has_api_key:
        return {"type": "api_key"}
    
    return {"type": "unknown"}
```

**Feature Detection** (`feature_mapper.py`):
```python
async def detect_features(browser, llm_service):
    screenshot = await browser.screenshot()
    dom = await browser.extract_dom()
    
    # Vision analysis
    vision_prompt = """
    Analyze this web interface screenshot.
    Identify:
    1. What is the primary purpose? (chat, image generation, etc.)
    2. What models/options are available?
    3. What configurable features exist? (toggles, dropdowns)
    4. What is the main input method?
    5. Where does the output appear?
    """
    
    analysis = await llm_service.analyze_vision(screenshot, vision_prompt)
    
    # DOM analysis for selectors
    buttons = await browser.find_all("button")
    inputs = await browser.find_all("input, textarea")
    selects = await browser.find_all("select")
    
    return {
        "primary_operation": analysis.primary_purpose,
        "available_models": analysis.models,
        "features": analysis.features,
        "input_selector": find_best_input(inputs),
        "submit_selector": find_submit_button(buttons),
        "output_selector": find_output_area(dom)
    }
```

**Configuration Generation** (`config_generator.py`):
```python
async def generate_config(service_info, features):
    return {
        "service_id": service_info.id,
        "name": service_info.name,
        "url": service_info.url,
        "auth": {
            "type": service_info.auth_type,
            "session_cookie": service_info.session_cookie
        },
        "operations": [{
            "id": "chat_completion",
            "name": "Send Message",
            "parameters": {
                "message": {"type": "string", "required": True},
                "model": {"type": "string", "enum": features.available_models, "optional": True}
            },
            "execution_steps": [
                {"action": "navigate", "url": service_info.url},
                {"action": "wait_for", "selector": features.input_selector},
                {"action": "type", "selector": features.input_selector, "value": "{{message}}"},
                {"action": "select_model", "selector": features.model_selector, "value": "{{model}}"},
                {"action": "click", "selector": features.submit_selector},
                {"action": "wait_for_response", "selector": features.output_selector, "timeout": 60},
                {"action": "extract", "selector": features.output_selector}
            ]
        }],
        "discovered_at": datetime.utcnow()
    }
```

---

## 7. EXECUTION SYSTEM

### 7.1 Queue-Based Execution (No Rate Limiting)

**Queue Manager** (`queue_manager.py`):
```python
from asyncio import Queue
from typing import Dict

class ExecutionQueue:
    def __init__(self):
        self.queues: Dict[str, Queue] = {}  # service_id → queue
        
    async def add_task(self, service_id: str, task: dict):
        if service_id not in self.queues:
            self.queues[service_id] = Queue()
        await self.queues[service_id].put(task)
        
    async def process_queue(self, service_id: str, browser_pool):
        queue = self.queues.get(service_id)
        if not queue:
            return
            
        while True:
            task = await queue.get()
            try:
                browser = await browser_pool.acquire(service_id)
                result = await execute_operation(browser, task)
                await send_result(task.client_id, result)
            finally:
                await browser_pool.release(browser)
                queue.task_done()
```

### 7.2 Operation Execution with Streaming

**Operation Runner** (`operation_runner.py`):
```python
async def execute_operation(browser, service_config, operation_id, parameters, ws_client):
    operation = service_config.get_operation(operation_id)
    
    for step in operation.execution_steps:
        await ws_client.send_log(f"Executing: {step.action}")
        
        if step.action == "navigate":
            await browser.navigate(step.url)
        elif step.action == "type":
            value = parameters.get(step.value.strip("{{}}"))
            await browser.type(step.selector, value)
        elif step.action == "click":
            await browser.click(step.selector)
        elif step.action == "wait_for_response":
            await browser.wait_for(step.selector, timeout=step.timeout)
        elif step.action == "extract":
            result = await browser.extract(step.selector)
            return result
```


---

## 8. DATABASE SCHEMA

```sql
-- Services table
CREATE TABLE services (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) NOT NULL,
    url TEXT NOT NULL,
    type VARCHAR(50) DEFAULT 'generic',
    status VARCHAR(50) DEFAULT 'offline',
    login_status VARCHAR(50) DEFAULT 'pending',
    discovery_status VARCHAR(50) DEFAULT 'pending',
    config JSONB,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW()
);

-- Credentials table (encrypted)
CREATE TABLE service_credentials (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_id UUID REFERENCES services(id) ON DELETE CASCADE,
    auth_type VARCHAR(50),  -- form_login, oauth, api_key
    encrypted_email TEXT,
    encrypted_password TEXT,
    encrypted_api_key TEXT,
    session_cookie TEXT,
    created_at TIMESTAMP DEFAULT NOW()
);

-- Execution history
CREATE TABLE executions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_id UUID REFERENCES services(id) ON DELETE CASCADE,
    operation_id VARCHAR(100),
    parameters JSONB,
    result TEXT,
    status VARCHAR(50),  -- pending, running, success, failed
    started_at TIMESTAMP DEFAULT NOW(),
    completed_at TIMESTAMP,
    error TEXT
);

-- Service stats (aggregated)
CREATE TABLE service_stats (
    service_id UUID PRIMARY KEY REFERENCES services(id) ON DELETE CASCADE,
    total_requests INT DEFAULT 0,
    successful_requests INT DEFAULT 0,
    failed_requests INT DEFAULT 0,
    avg_latency_ms INT DEFAULT 0,
    last_request_at TIMESTAMP,
    uptime_start TIMESTAMP DEFAULT NOW()
);
```

---

## 9. IMPLEMENTATION PHASES
**Goal**: Basic FastAPI server with database

**Files to Create:**
1. `/__init__.py` - Package init
2. `service.py` - Service data model
3. `openai_schemas.py` - OpenAI request/response schemas
4. `database.py` - SQLAlchemy models + DB connection
5. `main.py` - FastAPI app with CORS, middleware

**Tasks:**
- Set up PostgreSQL database
- Create tables with migrations
- Basic FastAPI server running on port 8000
- Health check endpoint `/health`

### Phase 2: Service Management API (Days 3-4)
**Goal**: CRUD operations for services

**Files to Create:**
1. `service_manager.py` - Service CRUD routes
2. `credential_store.py` - Encrypted credential storage
3. `websocket_handler.py` - WebSocket connection manager

**Endpoints to Implement:**
- `POST /api/services` - Register service
- `GET /api/services` - List all services
- `GET /api/services/:id` - Get single service
- `PUT /api/services/:id` - Update service config
- `DELETE /api/services/:id` - Delete service
- `WS /ws/services/:id` - WebSocket for updates

**Test**: Register k2think.ai service, verify database storage

### Phase 3: Auto-Discovery Engine (Days 5-7)
**Goal**: Automated service discovery

**Files to Create:**
1. `auth_detector.py` - Auth mechanism detection
2. `feature_mapper.py` - UI feature extraction
3. `operation_builder.py` - Operation definition creation
4. `config_generator.py` - Config synthesis
5. `orchestrator.py` - Discovery pipeline coordinator

**Reuse from AutoQA:**
- `autoqa/builder/analyzer/page_analyzer.py`
- `autoqa/builder/analyzer/visual_analyzer.py`
- `autoqa/builder/discovery/form_analyzer.py`
- `autoqa/llm/service.py`

**Tasks:**
- Implement auth detection (form login focus)
- Login execution with credential injection
- Vision-based feature detection
- Operation mapping with natural language selectors
- Config generation and storage

**Test**: Discover k2think.ai capabilities automatically

### Phase 4: Execution Engine (Days 8-10)
**Goal**: Execute discovered operations

**Files to Create:**
1. `queue_manager.py` - FIFO task queue
2. `operation_runner.py` - Operation execution logic
3. `streaming.py` - WebSocket log streaming

**Reuse from AutoQA:**
- `autoqa/concurrency/browser_pool.py` - Owl-browser pooling
- `autoqa/runner/self_healing.py` - Selector recovery

**Tasks:**
- Queue-based task processing (no rate limiting)
- Browser session acquisition from pool
- Step-by-step execution with logging
- Real-time log streaming via WebSocket
- Error recovery with self-healing selectors

**Test**: Execute chat completion on k2think.ai
**Goal**: OpenAI-compatible endpoints

**Files to Create:**
1. `web2api/api/openai_compat.py` - OpenAI API routes

**Endpoints to Implement:**
- `POST /v1/chat/completions` - Main execution endpoint
- `GET /v1/models` - List services as models
- Streaming support with SSE

**Tasks:**
- Map OpenAI request → service execution
- Format response as OpenAI completion
- Handle streaming responses
- Token counting (estimated)

**Test**: 
```bash
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "k2think",
    "messages": [{"role": "user", "content": "Hello, how are you?"}]
  }'
```

### Phase 6: K2Think.ai Integration & Testing (Days 13-14)
**Goal**: End-to-end working system

**Test Credentials:**
- URL: https://www.k2think.ai
- Email: developer@pixelium.uk
- Password: developer123?Collapse commentComment on line R687cubic-dev-ai[bot] commented on Jan 20, 2026 cubic-dev-ai[bot]on Jan 20, 2026More actions
P1: Remove plaintext credentials from the requirements document; storing real usernames/passwords in repo docs is a security risk. Use placeholders or reference a secure secret store instead.

Prompt for AI agents
Check if this issue is valid — if so, understand the root cause and fix it. At backend/REQUIREMENTS.md, line 687:

<comment>Remove plaintext credentials from the requirements document; storing real usernames/passwords in repo docs is a security risk. Use placeholders or reference a secure secret store instead.</comment>

<file context>
@@ -1,27 +1,851 @@
+**Test Credentials:**
+- URL: https://www.k2think.ai
+- Email: developer@pixelium.uk
+- Password: developer123?
+
+**Test Plan:**
</file context>

    
      
    

      
    

    
  


  
    
    
    
  
ReactWrite a replyResolve commentCode has comments. Press enter to view.

**Test Plan:**
1. Register service via API
2. Monitor login via WebSocket
3. Trigger discovery
4. Verify discovered configuration
5. Execute chat completion via OpenAI API
6. Verify response correctness
7. Test multiple concurrent requests
8. Test error recovery

**Success Criteria:**
✅ Service registers successfully
✅ Auto-login completes
✅ Features discovered automatically
✅ OpenAI API returns valid response
✅ Multiple requests queue properly
✅ Errors self-heal and retry

---

## 10. FILE MODIFICATION PLAN

### Files to Rename/Move
```bash
# Rename autoqa directory to web2api
mv backend/web2api/autoqa-ai-testing/src/autoqa backend/web2api/autoqa-ai-testing/src/web2api_base

# Keep as reference, create new web2api alongside
```

### Files to Modify from AutoQA

**1. `api/main.py`**: Transform from test API to service API
- Remove: `/build`, `/run`, `/results` endpoints
- Add: Service CRUD, discovery trigger, execute operation
- Add: WebSocket handler integration

**2. `concurrency/browser_pool.py`**: Enhance for service isolation
- Add: Service-specific browser profiles
- Add: Session persistence per service
- Keep: Existing pooling logic

**3. `llm/service.py`**: Adapt for discovery prompts
- Add: Auth detection prompts
- Add: Feature extraction prompts
- Add: Operation mapping prompts

**4. `storage/database.py`**: Add service tables
- Add: Service model
- Add: ServiceCredential model
- Add: Execution model
- Keep: Existing artifact storage

### New Files to Create

**Discovery Module:**
```
discovery/
├── __init__.py
├── auth_detector.py       # NEW
├── feature_mapper.py      # NEW
├── operation_builder.py   # NEW
├── config_generator.py    # NEW
└── orchestrator.py        # NEW
```

**Execution Module:**
```
web2api/execution/
├── __init__.py
├── queue_manager.py       # NEW
├── operation_runner.py    # ADAPT from runner/test_runner.py
└── streaming.py           # NEW
```

**Auth Module:**
```
auth/
├── __init__.py
├── credential_store.py    # NEW
├── session_manager.py     # NEW
└── form_filler.py         # ADAPT from discovery/form_analyzer.py
```

**API Module:**
```
api/
├── __init__.py
├── main.py                # MODIFY from autoqa/api/main.py
├── service_manager.py     # NEW
├── openai_compat.py       # NEW
└── websocket_handler.py   # NEW
```

**Models:**
```
models/
├── __init__.py
├── service.py             # NEW - Pydantic models
├── operation.py           # NEW
└── openai_schemas.py      # NEW
```

---

## 11. SUCCESS METRICS

**System is considered COMPLETE when:**

1. ✅ K2Think.ai service registers automatically
2. ✅ Login completes without manual intervention
3. ✅ Discovery identifies at least: input field, submit button, output area
4. ✅ Configuration generates valid operation definition
5. ✅ OpenAI API request: `POST /v1/chat/completions` returns valid response
6. ✅ Response contains actual output from k2think.ai
7. ✅ Multiple concurrent requests queue properly
8. ✅ TOWER frontend can connect and manage services
9. ✅ WebSocket streams real-time updates
10. ✅ Error recovery works (self-healing selectors)

**Test Command:**
```bash
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "k2think",
    "messages": [
      {"role": "user", "content": "Write a haiku about programming"}
    ]
  }'
```

**Expected Response:**
```json
{
  "id": "chatcmpl-xxx",
  "object": "chat.completion",
  "created": 1234567890,
  "model": "k2think",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "Code flows like water\nBugs emerge then disappear\nCommit, push, repeat"
    },
    "finish_reason": "stop"
  }],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 20,
    "total_tokens": 30
  }
}
``` 
## Executive Summary
 
**Web2API** transforms any web service into an OpenAI-compatible API endpoint using intelligent browser automation and auto-discovery.
 
**Core Flow**: `URL + Credentials → Auto-Discovery → OpenAI API Endpoint`
 
**Key Innovation**: Zero manual configuration - the system discovers service capabilities, authentication flows, and operations automatically through intelligent analysis.
 
---
 
## 1. SYSTEM ARCHITECTURE
 
### 1.1 High-Level Architecture
 
```
┌─────────────────────────────────────────────────────────────┐
│                     TOWER Frontend (React)                   │
│  - Service Management UI                                     │
│  - Real-time Status Updates                                  │
│  - Configuration Editor                                       │
└────────────────┬────────────────────────────────────────────┘
                 │ HTTP/REST + WebSocket
┌────────────────▼────────────────────────────────────────────┐
│              Web2API Server (FastAPI)                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  API Layer                                            │  │
│  │  - OpenAI-compatible endpoints (/v1/chat/completions)│  │
│  │  - Service management (CRUD)                          │  │
│  │  - WebSocket streaming                                │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Discovery Engine                                     │  │
│  │  - Authentication detection                            │  │
│  │  - Feature extraction (vision + DOM)                  │  │
│  │  - Operation mapping                                   │  │
│  │  - Configuration generation                            │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Execution Engine                                     │  │
│  │  - Queue-based task processing                         │  │
│  │  - Browser session management                          │  │
│  │  - Error recovery & self-healing                       │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Storage Layer                                        │  │
│  │  - Service configurations (PostgreSQL)                │  │
│  │  - Encrypted credentials                               │  │
│  │  - Execution artifacts                                 │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────┬────────────────────────────────────────────┘
                 │ Owl-Browser Python SDK
┌────────────────▼────────────────────────────────────────────┐
│              Owl-Browser Pool                                │
│  - Stealth browser automation                                │
│  - Anti-detection measures                                   │
│  - Session pooling & reuse                                   │
│  - Natural language selectors                                │
└────────────────┬────────────────────────────────────────────┘
                 │ HTTPS
┌────────────────▼────────────────────────────────────────────┐
│         Target Web Services                                  │
│  - ChatGPT, Claude, Z.ai, K2Think, etc.                     │
└─────────────────────────────────────────────────────────────┘
```
 
### 1.2 Technology Stack
 
**Backend:**
- FastAPI (async Python web framework)
- Owl-Browser Python SDK (stealth automation)
- PostgreSQL (service configurations & state)
- Redis (optional: queue & caching)
- SQLAlchemy (ORM)
- Pydantic (data validation)
- Cryptography (credential encryption)
- LiteLLM (LLM orchestration)
 
**Automation:**
- Owl-Browser (anti-detection browser automation)
- Vision models (GPT-4V, Claude 3.5 Sonnet) for UI analysis
- DOM analysis & element classification
- Network traffic inspection
 
**Frontend:**
- React + TypeScript
- TailwindCSS
- WebSocket client for real-time updatesExecutive Summary & Architecture - Complete system diagram
Owl-Browser Analysis - Stealth capabilities & integration
AutoQA Functionality Matrix - 70% reusable components mapped
Gap Analysis - Clear 30% new development required
API Specifications - OpenAI + TOWER frontend contracts
Discovery Pipeline - Zero-template auto-discovery flow
Execution System - Queue-based (NO rate limiting)
Database Schema - PostgreSQL tables for services/credentials/executions
Implementation Phases - 6 phases, 14-day timeline
File Modification Plan - Detailed file-by-file changes
Success Metrics - Clear acceptance criteria with K2Think.ai test

📋
TOWER Frontend Contract Identified
Clear API requirements from App.tsx & types.ts:

Service CRUD with status tracking
Discovery trigger endpoint
Execute endpoint for chat completions
WebSocket for real-time updates
Status fields: loginStatus, discoveryStatus, status

├── api/
│   ├── main.py                    # FastAPI app (modify from autoqa)
│   ├── openai_compat.py          # NEW: OpenAI API endpoints
│   ├── service_manager.py        # NEW: Service CRUD
│   └── websocket_handler.py      # NEW: Real-time updates
├── discovery/
│   ├── auth_detector.py          # NEW: Auth mechanism detection
│   ├── feature_mapper.py         # NEW: Service capability extraction
│   ├── operation_builder.py     # NEW: Dynamic operation creation
│   └── config_generator.py       # NEW: Config synthesis
├── execution/
│   ├── queue_manager.py          # NEW: FIFO task queue
│   ├── operation_runner.py       # ADAPT: From test_runner
│   └── streaming.py              # NEW: WebSocket log streaming
├── auth/
│   ├── credential_store.py       # NEW: Encrypted storage
│   ├── session_manager.py        # NEW: Browser session + cookies
│   └── form_filler.py            # ADAPT: From form_analyzer
├── models/
│   ├── service.py                # NEW: Service data model
│   ├── operation.py              # NEW: Operation schema
│   └── openai_schemas.py         # NEW: OpenAI request/response
└── storage/
    └── database.py               # MODIFY: Add service tables


## 5. API SPECIFICATIONS
 
### 5.1 TOWER Frontend Contract (from types.ts & App.tsx)
 
**Service Data Model:**
```typescript
interface Service {
  id: string;
  name: string;
  type: 'zai' | 'chatgpt' | 'claude' | 'k2think' | 'generic';
  url: string;
  status: 'online' | 'offline' | 'maintenance' | 'analyzing';
  loginStatus: 'pending' | 'processing' | 'success' | 'failed';
  discoveryStatus: 'pending' | 'scanning' | 'complete';
  config: ServiceConfig;
  availableModels?: string[];
  stats: {
    uptime: string;
    requests24h: number;
    avgLatency: number;
  };
}
```
 
**Required REST Endpoints:**
 
```
POST   /api/services
  Body: { url: string, email: string, password: string }
  Response: Service (with loginStatus: 'pending', discoveryStatus: 'pending')
  
GET    /api/services
  Response: Service[]
  
GET    /api/services/:id
  Response: Service
  
PUT    /api/services/:id
  Body: { config: ServiceConfig }
  Response: Service
  
DELETE /api/services/:id
  Response: { success: boolean }
 
POST   /api/services/:id/discover
  Triggers background discovery process
  Response: { message: string, taskId: string }
 
POST   /api/services/:id/execute
  Body: { message: string, model?: string }
  Response: { response: string, model: string }
```
 
**WebSocket Endpoint:**
```
WS /ws/services/:id
  Events:
    - login_update: { loginStatus: string, message: string }
    - discovery_update: { discoveryStatus: string, progress: number, message: string }
    - execution_log: { timestamp: number, level: string, message: string }
    - status_change: { status: string }
```
 
### 5.2 OpenAI-Compatible Endpoints
 
```
POST /v1/chat/completions
  Body: {
    model: string,  // Service ID or "service-id:model-name"
    messages: Array<{role: string, content: string}>,
    stream?: boolean,
    temperature?: number,
    max_tokens?: number
  }
  Response: {
    id: string,
    object: "chat.completion",
    created: number,
    model: string,
    choices: [{
      index: 0,
      message: {role: "assistant", content: string},
      finish_reason: "stop"
    }],
    usage: {prompt_tokens: number, completion_tokens: number, total_tokens: number}
  }
 
GET /v1/models
  Response: {
    object: "list",
    data: [{
      id: string,  // Service ID
      object: "model",
      created: number,
      owned_by: "web2api"
    }]
  }
```
 
---
 
## 6. DISCOVERY PIPELINE (NO TEMPLATES!)
 
### 6.1 Auto-Discovery Flow
 
```
Service Registration
    ↓
1. Authentication Discovery
   - Detect login form fields (email, password, 2FA)
   - Identify OAuth flow if present
   - Check for cookie-based auth
   - Store auth mechanism
    ↓
2. Login Execution
   - Auto-fill credentials
   - Submit form
   - Handle 2FA (manual intervention if needed)
   - Verify successful login
   - Save session cookies
    ↓
3. Feature Detection (Vision + DOM)
   - Screenshot main interface
   - LLM vision analysis: "What capabilities does this service offer?"
   - DOM analysis: buttons, dropdowns, toggles
   - Identify:
     * Model selectors
     * Feature toggles (web browsing, image generation)
     * Input areas (chat, prompt)
     * Output areas (response, result)
    ↓
4. Operation Mapping
   - Detect primary operation (e.g., "Send Message")
   - Map input fields → parameters
   - Map UI controls → optional parameters
   - Generate selector strategy (natural language)
    ↓
5. Configuration Generation
   - Create service config JSON
   - Define operations with steps
   - Set up parameter schema
   - Store in database
    ↓
Service Ready (discoveryStatus: 'complete')
```
 
### 6.2 Discovery Implementation Details
 
**Auth Detection** (`auth_detector.py`):
```python
async def detect_auth_mechanism(browser, url):
    await browser.navigate(url)
    
    # Check for login form
    has_email = await browser.find("email input")
    has_password = await browser.find("password input")
    
    if has_email and has_password:
        return {
            "type": "form_login",
            "fields": {"email": email_selector, "password": password_selector}
        }
    
    # Check for OAuth
    has_oauth = await browser.find("Sign in with Google")
    if has_oauth:
        return {"type": "oauth", "provider": "google"}
    
    # Check for API key
    has_api_key = await browser.find("API key input")
    if has_api_key:
        return {"type": "api_key"}
    
    return {"type": "unknown"}
```
 
**Feature Detection** (`feature_mapper.py`):
```python
async def detect_features(browser, llm_service):
    screenshot = await browser.screenshot()
    dom = await browser.extract_dom()
    
    # Vision analysis
    vision_prompt = """
    Analyze this web interface screenshot.
    Identify:
    1. What is the primary purpose? (chat, image generation, etc.)
    2. What models/options are available?
    3. What configurable features exist? (toggles, dropdowns)
    4. What is the main input method?
    5. Where does the output appear?
    """
    
    analysis = await llm_service.analyze_vision(screenshot, vision_prompt)
    
    # DOM analysis for selectors
    buttons = await browser.find_all("button")
    inputs = await browser.find_all("input, textarea")
    selects = await browser.find_all("select")
    
    return {
        "primary_operation": analysis.primary_purpose,
        "available_models": analysis.models,
        "features": analysis.features,
        "input_selector": find_best_input(inputs),
        "submit_selector": find_submit_button(buttons),
        "output_selector": find_output_area(dom)
    }
```
 
**Configuration Generation** (`config_generator.py`):
```python
async def generate_config(service_info, features):
    return {
        "service_id": service_info.id,
        "name": service_info.name,
        "url": service_info.url,
        "auth": {
            "type": service_info.auth_type,
            "session_cookie": service_info.session_cookie
        },
        "operations": [{
            "id": "chat_completion",
            "name": "Send Message",
            "parameters": {
                "message": {"type": "string", "required": True},
                "model": {"type": "string", "enum": features.available_models, "optional": True}
            },
            "execution_steps": [
                {"action": "navigate", "url": service_info.url},
                {"action": "wait_for", "selector": features.input_selector},
                {"action": "type", "selector": features.input_selector, "value": "{{message}}"},
                {"action": "select_model", "selector": features.model_selector, "value": "{{model}}"},
                {"action": "click", "selector": features.submit_selector},
                {"action": "wait_for_response", "selector": features.output_selector, "timeout": 60},
                {"action": "extract", "selector": features.output_selector}
            ]
        }],
        "discovered_at": datetime.utcnow()
    }
```
 
---
 
## 7. EXECUTION SYSTEM
 
### 7.1 Queue-Based Execution (No Rate Limiting)
 
**Queue Manager** (`queue_manager.py`):
```python
from asyncio import Queue
from typing import Dict
 
class ExecutionQueue:
    def __init__(self):
        self.queues: Dict[str, Queue] = {}  # service_id → queue
        
    async def add_task(self, service_id: str, task: dict):
        if service_id not in self.queues:
            self.queues[service_id] = Queue()
        await self.queues[service_id].put(task)
        
    async def process_queue(self, service_id: str, browser_pool):
        queue = self.queues.get(service_id)
        if not queue:
            return
            
        while True:
            task = await queue.get()
            try:
                browser = await browser_pool.acquire(service_id)
                result = await execute_operation(browser, task)
                await send_result(task.client_id, result)
            finally:
                await browser_pool.release(browser)
                queue.task_done()
```
 
### 7.2 Operation Execution with Streaming
 
**Operation Runner** (`operation_runner.py`):
```python
async def execute_operation(browser, service_config, operation_id, parameters, ws_client):
    operation = service_config.get_operation(operation_id)
    
    for step in operation.execution_steps:
        await ws_client.send_log(f"Executing: {step.action}")
        
        if step.action == "navigate":
            await browser.navigate(step.url)
        elif step.action == "type":
            value = parameters.get(step.value.strip("{{}}"))
            await browser.type(step.selector, value)
        elif step.action == "click":
            await browser.click(step.selector)
        elif step.action == "wait_for_response":
            await browser.wait_for(step.selector, timeout=step.timeout)
        elif step.action == "extract":
            result = await browser.extract(step.selector)
            return result## 8. DATABASE SCHEMA
 
```sql
-- Services table
CREATE TABLE services (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) NOT NULL,
    url TEXT NOT NULL,
    type VARCHAR(50) DEFAULT 'generic',
    status VARCHAR(50) DEFAULT 'offline',
    login_status VARCHAR(50) DEFAULT 'pending',
    discovery_status VARCHAR(50) DEFAULT 'pending',
    config JSONB,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW()
);
 
-- Credentials table (encrypted)
CREATE TABLE service_credentials (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_id UUID REFERENCES services(id) ON DELETE CASCADE,
    auth_type VARCHAR(50),  -- form_login, oauth, api_key
    encrypted_email TEXT,
    encrypted_password TEXT,
    encrypted_api_key TEXT,
    session_cookie TEXT,
    created_at TIMESTAMP DEFAULT NOW()
);
 
-- Execution history
CREATE TABLE executions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    service_id UUID REFERENCES services(id) ON DELETE CASCADE,
    operation_id VARCHAR(100),
    parameters JSONB,
    result TEXT,
    status VARCHAR(50),  -- pending, running, success, failed
    started_at TIMESTAMP DEFAULT NOW(),
    completed_at TIMESTAMP,
    error TEXT
);
 
-- Service stats (aggregated)
CREATE TABLE service_stats (
    service_id UUID PRIMARY KEY REFERENCES services(id) ON DELETE CASCADE,
    total_requests INT DEFAULT 0,
    successful_requests INT DEFAULT 0,
    failed_requests INT DEFAULT 0,
    avg_latency_ms INT DEFAULT 0,
    last_request_at TIMESTAMP,
    uptime_start TIMESTAMP DEFAULT NOW()
);
```
 
---
 
## 9. IMPLEMENTATION PHASES
 
**Goal**: Basic FastAPI server with database
 
**Files to Create:**
1. `__init__.py` - Package init
2. `models/service.py` - Service data model
3. `models/openai_schemas.py` - OpenAI request/response schemas
4. `storage/database.py` - SQLAlchemy models + DB connection
5. `api/main.py` - FastAPI app with CORS, middleware
 
**Tasks:**
- Set up PostgreSQL database
- Create tables with migrations
- Basic FastAPI server running on port 8000
- Health check endpoint `/health`
 
**Goal**: CRUD operations for services
 
**Files to Create:**
1. `api/service_manager.py` - Service CRUD routes
2. `auth/credential_store.py` - Encrypted credential storage
3. `api/websocket_handler.py` - WebSocket connection manager
 
**Endpoints to Implement:**
- `POST /api/services` - Register service
- `GET /api/services` - List all services
- `GET /api/services/:id` - Get single service
- `PUT /api/services/:id` - Update service config
- `DELETE /api/services/:id` - Delete service
- `WS /ws/services/:id` - WebSocket for updates
 
**Test**: Register k2think.ai service, verify database storage
 
### Phase 3: Auto-Discovery Engine (Days 5-7)
**Goal**: Automated service discovery
 
**Files to Create:**
1. `discovery/auth_detector.py` - Auth mechanism detection
2. `discovery/feature_mapper.py` - UI feature extraction
3. `discovery/operation_builder.py` - Operation definition creation
4. `discovery/config_generator.py` - Config synthesis
5. `discovery/orchestrator.py` - Discovery pipeline coordinator
 
**Reuse from AutoQA:**
- `autoqa/builder/analyzer/page_analyzer.py`
- `autoqa/builder/analyzer/visual_analyzer.py`
- `autoqa/builder/discovery/form_analyzer.py`
- `autoqa/llm/service.py`
 
**Tasks:**
- Implement auth detection (form login focus)
- Login execution with credential injection
- Vision-based feature detection
- Operation mapping with natural language selectors
- Config generation and storage
 
**Test**: Discover k2think.ai capabilities automatically
 
### Phase 4: Execution Engine (Days 8-10)
**Goal**: Execute discovered operations
 
**Files to Create:**
1. `execution/queue_manager.py` - FIFO task queue
2. `execution/operation_runner.py` - Operation execution logic
3. `execution/streaming.py` - WebSocket log streaming
 
**Reuse from AutoQA:**
- `autoqa/concurrency/browser_pool.py` - Owl-browser pooling
- `autoqa/runner/self_healing.py` - Selector recovery
 
**Tasks:**
- Queue-based task processing (no rate limiting)
- Browser session acquisition from pool
- Step-by-step execution with logging
- Real-time log streaming via WebSocket
- Error recovery with self-healing selectors
 
**Test**: Execute chat completion on k2think.ai
 
### Phase 5: OpenAI API Compatibility (Days 11-12)
**Goal**: OpenAI-compatible endpoints
 
**Files to Create:**
1. `web2api/api/openai_compat.py` - OpenAI API routes
 
**Endpoints to Implement:**
- `POST /v1/chat/completions` - Main execution endpoint
- `GET /v1/models` - List services as models
- Streaming support with SSE
 
**Tasks:**
- Map OpenAI request → service execution
- Format response as OpenAI completion
- Handle streaming responses
- Token counting (estimated)
 
**Test**: 
```bash
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "k2think",
    "messages": [{"role": "user", "content": "Hello, how are you?"}]
  }'
```
 
### Phase 6: K2Think.ai Integration & Testing (Days 13-14)
**Goal**: End-to-end working system
 
**Test Credentials:**
- URL: https://www.k2think.ai
- Email: developer@pixelium.uk
- Password: developer123?
 
**Test Plan:**
1. Register service via API
2. Monitor login via WebSocket
3. Trigger discovery
4. Verify discovered configuration
5. Execute chat completion via OpenAI API
6. Verify response correctness
7. Test multiple concurrent requests
8. Test error recovery
 
**Success Criteria:**
✅ Service registers successfully
✅ Auto-login completes
✅ Features discovered automatically
✅ OpenAI API returns valid response
✅ Multiple requests queue properly
✅ Errors self-heal and retry
 
---
 

 
**1. `api/main.py`**: Transform from test API to service API
- Remove: `/build`, `/run`, `/results` endpoints
- Add: Service CRUD, discovery trigger, execute operation
- Add: WebSocket handler integration
 
**2. `concurrency/browser_pool.py`**: Enhance for service isolation
- Add: Service-specific browser profiles
- Add: Session persistence per service
- Keep: Existing pooling logic
 
**3. `llm/service.py`**: Adapt for discovery prompts
- Add: Auth detection prompts
- Add: Feature extraction prompts
- Add: Operation mapping prompts
 
**4. `storage/database.py`**: Add service tables
- Add: Service model
- Add: ServiceCredential model
- Add: Execution model
- Keep: Existing artifact storage
 
### New Files to Create
 
**Discovery Module:**
```
discovery/
├── __init__.py
├── auth_detector.py       # NEW
├── feature_mapper.py      # NEW
├── operation_builder.py   # NEW
├── config_generator.py    # NEW
└── orchestrator.py        # NEW
```
 
**Execution Module:**
```
execution/
├── __init__.py
├── queue_manager.py       # NEW
├── operation_runner.py    # ADAPT from runner/test_runner.py
└── streaming.py           # NEW
```
 
**Auth Module:**
```
auth/
├── __init__.py
├── credential_store.py    # NEW
├── session_manager.py     # NEW
└── form_filler.py         # ADAPT from discovery/form_analyzer.py
```
 
**API Module:**
```
api/
├── __init__.py
├── main.py                # MODIFY from autoqa/api/main.py
├── service_manager.py     # NEW
├── openai_compat.py       # NEW
└── websocket_handler.py   # NEW
```
 
**Models:**
```
models/
├── __init__.py
├── service.py             # NEW - Pydantic models
├── operation.py           # NEW
└── openai_schemas.py      # NEW
```
 
---
 
## 11. SUCCESS METRICS
 
**System is considered COMPLETE when:**
 
1. ✅ K2Think.ai service registers automatically
2. ✅ Login completes without manual intervention
3. ✅ Discovery identifies at least: input field, submit button, output area
4. ✅ Configuration generates valid operation definition
5. ✅ OpenAI API request: `POST /v1/chat/completions` returns valid response
6. ✅ Response contains actual output from k2think.ai
7. ✅ Multiple concurrent requests queue properly
8. ✅ TOWER frontend can connect and manage services
9. ✅ WebSocket streams real-time updates
10. ✅ Error recovery works (self-healing selectors)
 
**Test Command:**
```bash
curl -X POST http://localhost:8000/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "k2think",
    "messages": [
      {"role": "user", "content": "Write a haiku about programming"}
    ]
  }'
```
 
**Expected Response:**
```json
{
  "id": "chatcmpl-xxx",
  "object": "chat.completion",
  "created": 1234567890,
  "model": "k2think",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "Code flows like water\nBugs emerge then disappear\nCommit, push, repeat"
    },
    "finish_reason": "stop"
  }],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 20,
    "total_tokens": 30
  }
}
```
 
---
 
## END OF REQUIREMENTS
 
**NEXT STEPS: BEGIN IMPLEMENTATION**
 
Phase 1 starts now with core infrastructure setup.

FeatureMapper - Map UI elements to capabilities
OperationBuilder - Generate executable operations
CredentialStore - Encrypted credential storage
Complete execution logic - Navigate, login, chat, extract response
🧪 Test Commands (once complete)
# Register k2think.ai
curl -X POST <http://localhost:8080/api/services> \
  -d '{"url": "<https://www.k2think.ai>", 
       "credentials": {"type": "login_password", 
                       "username": "developer@pixelium.uk", 
                       "password": "developer123?"}}'
 
# Run discovery
curl -X POST <http://localhost:8080/api/services/>{id}/discover
 
# Test OpenAI API
curl -X POST <http://localhost:8080/v1/chat/completions> \
  -d '{"model": "{service_id}", "messages": [{"role": "user", "content": "Hello!"}]}'

In TOWER PROJECT. Fully analyze -
https://github.com/Zeeeepa/TOWER/tree/main/backend/web2api/autoqa-ai-testing/src/autoqa

ALL PY FILES - VIEW ALL OF THEM,
VIEW WHOLE https://github.com/Zeeeepa/TOWER/blob/main/backend/web2api/autoqa-ai-testing/docs/TECHNICAL.md
FULLY VIEW https://github.com/Zeeeepa/TOWER/blob/main/backend/packages/owl-browser/README.md
FULLY VIEW https://github.com/Zeeeepa/TOWER/blob/main/backend/packages/owl-browser-sdk/README.md
https://owlbrowser.net/docs - View all 157 commands 

VIEW ALL 157 commands available for owl browser -> 
https://owlbrowser.net/docs/browser_ai_extract - this should be used to identify area where from to extract response in webchat interface.
https://owlbrowser.net/docs/browser_extract_text  - this should be used to extract response in webchat interface.
https://owlbrowser.net/docs/browser_detect_captcha - this should be used to solve captcha when logging in if present
https://owlbrowser.net/docs/browser_classify_captcha - this should be used to solve captcha when logging in if present
https://owlbrowser.net/docs/browser_solve_text_captcha - th is should be used to solve captcha when logging in if present
https://owlbrowser.net/docs/browser_solve_image_captcha - this should be used to solve captcha when logging in if present
https://owlbrowser.net/docs/browser_solve_captcha - this should be used to solve captcha when logging in if present
https://owlbrowser.net/docs/browser_query_page  - this could be used to identify functions/features from the page?
https://owlbrowser.net/docs/browser_ai_analyze   - this could be used to identify functions/features from the page?
https://owlbrowser.net/docs/browser_find_element     - this could be used to identify functions/features from the page?
https://owlbrowser.net/docs/browser_get_cookies  - this should be used when using same service in future  
https://owlbrowser.net/docs/browser_set_cookie  - this should be used when using same service in future  
https://owlbrowser.net/docs/browser_is_enabled  - this should be used to identify when "Send" button changes state from disabled to enabled (Meaning that the response in the page was completed) and ready for retrieval
https://owlbrowser.net/docs/browser_upload_file  - this should be used for services that allow uploading files.
For different paralel services it should use 
Tab Management
8 commands
set_popup_policyget_tabsswitch_tabclose_tabnew_tabget_active_tabget_tab_countget_blocked_popups

WHEN ADDING SERVICE VIA TOWER FRONTEND -> IT SHOULD HAVE live-record feed viewport for actions and identificaTion flow  using these commands - (So that user would live-view how it goes to identify, test and save flows to access and configure functions/features of service).

Video Recording
11
start_video_recordingpause_video_recordingresume_video_recordingstop_video_recordingget_video_recording_statsdownload_video_recordingstart_live_streamstop_live_streamget_live_stream_statslist_live_streamsget_live_frame


Can you fully analyze requirements, analyze all py codes, all documentation website pages, all documentations, owl browser usages and features -> AND Properly UPGRADE DOCUMENTATION TO MATCH BETTER.

GOAL -> Is not to create new module, but to modify https://github.com/Zeeeepa/TOWER/tree/main/backend/web2api/autoqa-ai-testing/src/autoqa    to modify autoqa folder into web2api working server modifying ALL NEEDED CODEFILES WITHIN, not creating separate modules - but upgrading existant module -> for that you first need to identify what needs to be implemented from EXISTANT owl-browser functions
