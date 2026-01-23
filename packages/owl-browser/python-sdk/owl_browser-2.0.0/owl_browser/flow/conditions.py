"""
Condition evaluation for flow execution.

Provides condition evaluation logic for conditional branching
in flows.
"""

from __future__ import annotations

import re
from typing import Any

from ..types import ConditionOperator, FlowCondition
from .variables import get_value_at_path


def evaluate_condition(condition: FlowCondition, value: Any) -> bool:
    """
    Evaluate a condition against a value.

    Args:
        condition: The condition to evaluate.
        value: The value to check (usually the previous step result).

    Returns:
        True if the condition is satisfied, False otherwise.

    Example:
        ```python
        from owl_browser.types import FlowCondition, ConditionOperator
        from owl_browser.flow import evaluate_condition

        result = {"success": True, "count": 5}

        # Check if success is truthy
        cond = FlowCondition(
            source="previous",
            operator=ConditionOperator.IS_TRUTHY,
            field="success"
        )
        evaluate_condition(cond, result)  # True

        # Check if count > 3
        cond = FlowCondition(
            source="previous",
            operator=ConditionOperator.GREATER_THAN,
            field="count",
            value=3
        )
        evaluate_condition(cond, result)  # True
        ```
    """
    check_value = value
    if condition.field:
        check_value = get_value_at_path(value, condition.field)

    match condition.operator:
        case ConditionOperator.EQUALS:
            return check_value == condition.value

        case ConditionOperator.NOT_EQUALS:
            return check_value != condition.value

        case ConditionOperator.CONTAINS:
            if isinstance(check_value, str) and isinstance(condition.value, str):
                return condition.value in check_value
            if isinstance(check_value, (list, tuple)):
                return condition.value in check_value
            return False

        case ConditionOperator.NOT_CONTAINS:
            if isinstance(check_value, str) and isinstance(condition.value, str):
                return condition.value not in check_value
            if isinstance(check_value, (list, tuple)):
                return condition.value not in check_value
            return True

        case ConditionOperator.STARTS_WITH:
            if isinstance(check_value, str) and isinstance(condition.value, str):
                return check_value.startswith(condition.value)
            return False

        case ConditionOperator.ENDS_WITH:
            if isinstance(check_value, str) and isinstance(condition.value, str):
                return check_value.endswith(condition.value)
            return False

        case ConditionOperator.GREATER_THAN:
            if isinstance(check_value, (int, float)) and isinstance(
                condition.value, (int, float)
            ):
                return check_value > condition.value
            return False

        case ConditionOperator.LESS_THAN:
            if isinstance(check_value, (int, float)) and isinstance(
                condition.value, (int, float)
            ):
                return check_value < condition.value
            return False

        case ConditionOperator.IS_TRUTHY:
            return bool(check_value)

        case ConditionOperator.IS_FALSY:
            return not bool(check_value)

        case ConditionOperator.IS_EMPTY:
            if check_value is None:
                return True
            if isinstance(check_value, str):
                return len(check_value) == 0
            if isinstance(check_value, (list, tuple, dict)):
                return len(check_value) == 0
            return False

        case ConditionOperator.IS_NOT_EMPTY:
            if check_value is None:
                return False
            if isinstance(check_value, str):
                return len(check_value) > 0
            if isinstance(check_value, (list, tuple, dict)):
                return len(check_value) > 0
            return True

        case ConditionOperator.REGEX_MATCH:
            if isinstance(check_value, str) and isinstance(condition.value, str):
                try:
                    pattern = re.compile(condition.value)
                    return pattern.search(check_value) is not None
                except re.error:
                    return False
            return False

        case _:
            return False
