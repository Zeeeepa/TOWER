#!/bin/bash
# Cleanup script for owl-browser processes
# Use this if tests leave processes running

echo "🧹 Cleaning up owl-browser processes..."

# Kill llama-server processes
LLAMA_PIDS=$(pgrep -f "llama-server")
if [ ! -z "$LLAMA_PIDS" ]; then
  echo "  Killing llama-server processes: $LLAMA_PIDS"
  echo "$LLAMA_PIDS" | xargs kill -9 2>/dev/null
  echo "  ✓ llama-server cleaned up"
else
  echo "  ✓ No llama-server processes found"
fi

# Kill owl_browser processes
BROWSER_PIDS=$(pgrep -f "owl_browser")
if [ ! -z "$BROWSER_PIDS" ]; then
  echo "  Killing owl_browser processes: $BROWSER_PIDS"
  echo "$BROWSER_PIDS" | xargs kill -9 2>/dev/null
  echo "  ✓ owl_browser cleaned up"
else
  echo "  ✓ No owl_browser processes found"
fi

# Kill node mcp-server processes
MCP_PIDS=$(pgrep -f "mcp-server.cjs")
if [ ! -z "$MCP_PIDS" ]; then
  echo "  Killing mcp-server processes: $MCP_PIDS"
  echo "$MCP_PIDS" | xargs kill -9 2>/dev/null
  echo "  ✓ mcp-server cleaned up"
else
  echo "  ✓ No mcp-server processes found"
fi

# Clean up registry files
REGISTRY_DIR="/tmp/owl_browser_registry"
if [ -d "$REGISTRY_DIR" ]; then
  echo "  Cleaning registry: $REGISTRY_DIR"
  rm -rf "$REGISTRY_DIR"
  echo "  ✓ Registry cleaned up"
fi

echo "✨ Cleanup complete!"
