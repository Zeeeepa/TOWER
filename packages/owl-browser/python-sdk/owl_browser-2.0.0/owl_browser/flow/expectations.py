"""
Expectation validation for flow execution.

Provides validation of step results against expected values,
supporting various comparison types like equals, contains,
length, greater_than, etc.
"""

from __future__ import annotations

import json
import re
from typing import Any

from ..types import ExpectationResult, StepExpectation
from .variables import get_value_at_path


def check_expectation(result: Any, expected: StepExpectation) -> ExpectationResult:
    """
    Check if a result meets the expectation.

    Args:
        result: The actual result value to check.
        expected: The expectation to validate against.

    Returns:
        ExpectationResult indicating if the check passed.

    Example:
        ```python
        from owl_browser.types import StepExpectation
        from owl_browser.flow import check_expectation

        result = {"count": 5, "items": [1, 2, 3]}

        # Check exact equality
        exp = StepExpectation(equals=5, field="count")
        check = check_expectation(result, exp)
        # check.passed = True

        # Check array length
        exp = StepExpectation(length=3, field="items")
        check = check_expectation(result, exp)
        # check.passed = True
        ```
    """
    value_to_check = result
    if expected.field:
        value_to_check = get_value_at_path(result, expected.field)

    if expected.equals is not None:
        actual_json = json.dumps(value_to_check, sort_keys=True, default=str)
        expected_json = json.dumps(expected.equals, sort_keys=True, default=str)
        passed = actual_json == expected_json
        return ExpectationResult(
            passed=passed,
            message=(
                "Value matches expected"
                if passed
                else f"Expected {expected.equals!r}, got {value_to_check!r}"
            ),
            expected=expected.equals,
            actual=value_to_check,
        )

    if expected.contains is not None:
        str_value = str(value_to_check) if value_to_check is not None else ""
        passed = expected.contains in str_value
        return ExpectationResult(
            passed=passed,
            message=(
                f'Value contains "{expected.contains}"'
                if passed
                else f'Expected to contain "{expected.contains}", got "{str_value}"'
            ),
            expected=expected.contains,
            actual=str_value,
        )

    if expected.length is not None:
        if isinstance(value_to_check, (list, tuple)):
            actual_length = len(value_to_check)
        elif isinstance(value_to_check, str):
            actual_length = len(value_to_check)
        else:
            actual_length = 0
        passed = actual_length == expected.length
        return ExpectationResult(
            passed=passed,
            message=(
                f"Length is {expected.length}"
                if passed
                else f"Expected length {expected.length}, got {actual_length}"
            ),
            expected=expected.length,
            actual=actual_length,
        )

    if expected.greater_than is not None:
        try:
            num = float(value_to_check) if value_to_check is not None else float("nan")
        except (TypeError, ValueError):
            num = float("nan")
        passed = num > expected.greater_than
        return ExpectationResult(
            passed=passed,
            message=(
                f"Value {num} > {expected.greater_than}"
                if passed
                else f"Expected > {expected.greater_than}, got {num}"
            ),
            expected=f"> {expected.greater_than}",
            actual=num,
        )

    if expected.less_than is not None:
        try:
            num = float(value_to_check) if value_to_check is not None else float("nan")
        except (TypeError, ValueError):
            num = float("nan")
        passed = num < expected.less_than
        return ExpectationResult(
            passed=passed,
            message=(
                f"Value {num} < {expected.less_than}"
                if passed
                else f"Expected < {expected.less_than}, got {num}"
            ),
            expected=f"< {expected.less_than}",
            actual=num,
        )

    if expected.not_empty is not None:
        # Determine if value is empty
        is_empty = False
        if value_to_check is None:
            is_empty = True
        elif isinstance(value_to_check, str):
            is_empty = len(value_to_check) == 0
        elif isinstance(value_to_check, (list, tuple)):
            is_empty = len(value_to_check) == 0
        elif isinstance(value_to_check, dict):
            is_empty = len(value_to_check) == 0
        # Other types (int, float, bool, etc.) are considered "not empty"

        if expected.not_empty:
            # not_empty=True means value should NOT be empty
            passed = not is_empty
            return ExpectationResult(
                passed=passed,
                message=(
                    "Value is not empty"
                    if passed
                    else "Expected non-empty value, got empty"
                ),
                expected="non-empty",
                actual=value_to_check,
            )
        else:
            # not_empty=False means value SHOULD be empty
            passed = is_empty
            return ExpectationResult(
                passed=passed,
                message=(
                    "Value is empty"
                    if passed
                    else "Expected empty value, got non-empty"
                ),
                expected="empty",
                actual=value_to_check,
            )

    if expected.matches is not None:
        str_value = str(value_to_check) if value_to_check is not None else ""
        try:
            pattern = re.compile(expected.matches)
            passed = pattern.search(str_value) is not None
        except re.error:
            passed = False
        return ExpectationResult(
            passed=passed,
            message=(
                f"Value matches pattern /{expected.matches}/"
                if passed
                else f'Expected to match /{expected.matches}/, got "{str_value}"'
            ),
            expected=expected.matches,
            actual=str_value,
        )

    return ExpectationResult(
        passed=True,
        message="No expectation specified",
    )
