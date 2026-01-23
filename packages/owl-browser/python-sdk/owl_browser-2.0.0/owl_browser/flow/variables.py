"""
Variable resolution for flow execution.

Supports ${prev} and ${prev.field.subfield} and ${prev[0].id} syntax
for referencing previous step results.
"""

from __future__ import annotations

import re
from typing import Any


def get_value_at_path(obj: Any, path: str) -> Any:
    """
    Get value at a dot-notation path (e.g., 'data.items.0.name').

    Also supports bracket notation for array indices (e.g., '[0].id').

    Args:
        obj: The object to traverse.
        path: Dot-notation path to the value.

    Returns:
        The value at the path, or None if not found.
    """
    if not path:
        return obj

    normalized_path = (
        path.lstrip("[")
        .replace("][", ".")
        .replace("]", "")
        .replace("[", ".")
        .lstrip(".")
    )

    parts = normalized_path.split(".")
    current: Any = obj

    for part in parts:
        if current is None:
            return None

        if isinstance(current, dict):
            current = current.get(part)
        elif isinstance(current, (list, tuple)):
            try:
                index = int(part)
                current = current[index] if 0 <= index < len(current) else None
            except ValueError:
                return None
        else:
            return None

    return current


def _resolve_variable_in_string(value: str, previous_result: Any) -> Any:
    """
    Resolve variable references in a string value.

    Supports:
        ${prev} - entire previous result
        ${prev[0].id} - access path in previous result
        ${prev.field} - access field in previous result

    Args:
        value: String that may contain variable references.
        previous_result: The result from the previous step.

    Returns:
        The resolved value.
    """
    full_match = re.match(r"^\$\{prev([\[\.].*?)?\}$", value)
    if full_match:
        path = full_match.group(1) or ""
        return get_value_at_path(previous_result, path)

    def replace_var(match: re.Match[str]) -> str:
        path = match.group(1) or ""
        resolved = get_value_at_path(previous_result, path)
        if resolved is None:
            return ""
        if isinstance(resolved, str):
            return resolved
        if isinstance(resolved, (dict, list)):
            import json
            return json.dumps(resolved)
        return str(resolved)

    return re.sub(r"\$\{prev([\[\.].*?)?\}", replace_var, value)


def resolve_variables(params: dict[str, Any], previous_result: Any) -> dict[str, Any]:
    """
    Recursively resolve variable references in params object.

    Variable syntax:
        ${prev} - reference the entire previous result
        ${prev.field} - reference a field in the previous result
        ${prev[0]} - reference an array element
        ${prev[0].id} - reference a field in an array element

    Args:
        params: Dictionary of parameters that may contain variable references.
        previous_result: The result from the previous step.

    Returns:
        New dictionary with all variables resolved.

    Example:
        ```python
        previous = {"users": [{"id": 123, "name": "Alice"}]}
        params = {"user_id": "${prev.users[0].id}"}
        resolved = resolve_variables(params, previous)
        # resolved = {"user_id": 123}
        ```
    """
    resolved: dict[str, Any] = {}

    for key, value in params.items():
        if isinstance(value, str):
            resolved[key] = _resolve_variable_in_string(value, previous_result)
        elif isinstance(value, dict):
            resolved[key] = resolve_variables(value, previous_result)
        elif isinstance(value, list):
            resolved[key] = [
                (
                    _resolve_variable_in_string(item, previous_result)
                    if isinstance(item, str)
                    else (
                        resolve_variables(item, previous_result)
                        if isinstance(item, dict)
                        else item
                    )
                )
                for item in value
            ]
        else:
            resolved[key] = value

    return resolved
