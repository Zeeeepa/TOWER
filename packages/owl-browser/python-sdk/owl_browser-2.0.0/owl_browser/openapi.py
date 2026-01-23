"""
OpenAPI schema loading and dynamic method generation.

This module loads the bundled OpenAPI schema and generates tool definitions
that can be used to create dynamic methods. The schema is loaded at import
time from the bundled openapi.json file for offline use.
"""

from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path
from typing import Any, Final

from .exceptions import OpenAPISchemaError
from .types import ParameterDef, ToolDefinition

# Explicit exports for mypy --strict
# Note: BUNDLED_SCHEMA is provided via __getattr__ for backwards compatibility
__all__ = [
    "OpenAPILoader",
    "ToolDefinition",
    "ParameterDef",
    "coerce_integer_fields",
    "get_bundled_schema",
]

# Path to the bundled OpenAPI schema file
_SCHEMA_PATH: Final[Path] = Path(__file__).parent / "openapi.json"


@lru_cache(maxsize=1)
def _load_bundled_schema() -> dict[str, Any]:
    """
    Load the bundled OpenAPI schema from file.

    This function is cached to ensure the schema is only loaded once,
    regardless of how many times it's accessed.

    Returns:
        The OpenAPI schema dictionary.

    Raises:
        OpenAPISchemaError: If the schema file cannot be loaded.
    """
    try:
        content = _SCHEMA_PATH.read_text(encoding="utf-8")
        schema: dict[str, Any] = json.loads(content)
        return schema
    except FileNotFoundError as e:
        raise OpenAPISchemaError(
            f"Bundled OpenAPI schema not found at {_SCHEMA_PATH}. "
            "The SDK package may be corrupted."
        ) from e
    except json.JSONDecodeError as e:
        raise OpenAPISchemaError(f"Invalid JSON in bundled OpenAPI schema: {e}") from e
    except Exception as e:
        raise OpenAPISchemaError(f"Failed to load bundled OpenAPI schema: {e}") from e


def get_bundled_schema() -> dict[str, Any]:
    """
    Get the bundled OpenAPI schema.

    This is the primary way to access the bundled schema. The schema
    is loaded lazily on first access and cached thereafter.

    Returns:
        The OpenAPI schema dictionary.

    Example:
        ```python
        from owl_browser.openapi import get_bundled_schema

        schema = get_bundled_schema()
        print(schema["info"]["version"])
        ```
    """
    return _load_bundled_schema()


class OpenAPILoader:
    """
    Loads and parses OpenAPI schema to extract tool definitions.

    This class is responsible for parsing the OpenAPI schema and extracting
    tool definitions including parameter types, requirements, and descriptions.

    It also tracks which fields should be integers for type coercion,
    since JSON/OpenAPI uses 'number' for both int and float, but the
    HTTP API may expect integers.

    Example:
        ```python
        from owl_browser.openapi import OpenAPILoader

        # Load from file
        loader = OpenAPILoader.from_file("openapi.json")

        # Or from dict
        schema = {"openapi": "3.0.3", "paths": {...}}
        loader = OpenAPILoader(schema)

        # Access tools
        for name, tool in loader.tools.items():
            print(f"{name}: {tool.description}")
        ```
    """

    __slots__ = ("_schema", "_tools")

    def __init__(self, schema: dict[str, Any]) -> None:
        """
        Initialize OpenAPI loader with schema dictionary.

        Args:
            schema: OpenAPI schema dictionary.

        Raises:
            OpenAPISchemaError: If schema is invalid.
        """
        self._schema = schema
        self._tools: dict[str, ToolDefinition] = {}
        self._parse_schema()

    @classmethod
    def from_file(cls, path: str | Path) -> OpenAPILoader:
        """
        Load OpenAPI schema from a JSON file.

        Args:
            path: Path to the OpenAPI JSON file.

        Returns:
            OpenAPILoader instance.

        Raises:
            OpenAPISchemaError: If file cannot be read or parsed.
        """
        try:
            file_path = Path(path)
            content = file_path.read_text(encoding="utf-8")
            schema = json.loads(content)
            return cls(schema)
        except FileNotFoundError as e:
            raise OpenAPISchemaError(f"OpenAPI schema file not found: {path}") from e
        except json.JSONDecodeError as e:
            raise OpenAPISchemaError(f"Invalid JSON in OpenAPI schema: {e}") from e
        except Exception as e:
            raise OpenAPISchemaError(f"Failed to load OpenAPI schema: {e}") from e

    @classmethod
    def from_json(cls, json_str: str) -> OpenAPILoader:
        """
        Load OpenAPI schema from a JSON string.

        Args:
            json_str: JSON string containing the OpenAPI schema.

        Returns:
            OpenAPILoader instance.

        Raises:
            OpenAPISchemaError: If JSON is invalid.
        """
        try:
            schema = json.loads(json_str)
            return cls(schema)
        except json.JSONDecodeError as e:
            raise OpenAPISchemaError(f"Invalid JSON in OpenAPI schema: {e}") from e

    @property
    def tools(self) -> dict[str, ToolDefinition]:
        """Get all parsed tool definitions."""
        return self._tools

    def get_tool(self, name: str) -> ToolDefinition | None:
        """
        Get a specific tool definition by name.

        Args:
            name: Tool name (e.g., 'browser_navigate').

        Returns:
            ToolDefinition or None if not found.
        """
        return self._tools.get(name)

    def _parse_schema(self) -> None:
        """Parse the OpenAPI schema and extract tool definitions."""
        paths = self._schema.get("paths", {})

        for path, path_item in paths.items():
            # Include all execute paths, not just browser_* tools
            if not path.startswith("/api/execute/"):
                continue

            post_op = path_item.get("post")
            if not post_op:
                continue

            tool_name = path.split("/")[-1]
            self._tools[tool_name] = self._parse_tool(tool_name, post_op)

    def _parse_tool(self, name: str, operation: dict[str, Any]) -> ToolDefinition:
        """Parse a single tool operation."""
        description = operation.get("description") or operation.get("summary") or f"Execute {name}"

        request_body = operation.get("requestBody", {})
        content = request_body.get("content", {})
        json_content = content.get("application/json", {})
        schema = json_content.get("schema", {})

        parameters: dict[str, ParameterDef] = {}
        required_params: list[str] = schema.get("required", [])
        integer_fields: set[str] = set()

        properties = schema.get("properties", {})
        for prop_name, prop_def in properties.items():
            param_type = prop_def.get("type", "string")

            if param_type == "integer":
                integer_fields.add(prop_name)
                param_type = "number"

            enum_values = prop_def.get("enum")
            default_value = prop_def.get("default")

            parameters[prop_name] = ParameterDef(
                name=prop_name,
                type=param_type,
                required=prop_name in required_params,
                description=prop_def.get("description", ""),
                enum_values=enum_values,
                default=default_value,
            )

        return ToolDefinition(
            name=name,
            description=description,
            parameters=parameters,
            required_params=required_params,
            integer_fields=frozenset(integer_fields),
        )


def coerce_integer_fields(
    tool: ToolDefinition, params: dict[str, Any]
) -> dict[str, Any]:
    """
    Coerce numeric values to integers for fields that require it.

    The HTTP API expects integers but JSON may send floats like 200.0.
    This function ensures such fields are properly converted.

    Args:
        tool: Tool definition containing integer field information.
        params: Original parameters.

    Returns:
        Parameters with integer fields coerced.
    """
    if not tool.integer_fields:
        return params

    coerced = dict(params)
    for field in tool.integer_fields:
        if field in coerced and isinstance(coerced[field], float):
            coerced[field] = int(coerced[field])
    return coerced


# Backwards-compatible alias for the bundled schema.
# This is loaded lazily from the bundled openapi.json file.
# Use get_bundled_schema() for the function-based API.
def __getattr__(name: str) -> Any:
    """Module-level __getattr__ for lazy loading of BUNDLED_SCHEMA."""
    if name == "BUNDLED_SCHEMA":
        return get_bundled_schema()
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
