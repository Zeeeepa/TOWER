"""
Flow execution engine for Owl Browser SDK v2.

Provides the FlowExecutor class that executes flows with variable resolution,
expectation validation, and conditional branching support.
"""

from __future__ import annotations

import asyncio
import json
import time
import uuid
from pathlib import Path
from typing import TYPE_CHECKING, Any, Final, Literal

from ..exceptions import FlowExecutionError
from ..types import (
    Flow,
    FlowCondition,
    FlowResult,
    FlowStep,
    StepExpectation,
    StepResult,
)
from .conditions import evaluate_condition
from .expectations import check_expectation
from .variables import resolve_variables

if TYPE_CHECKING:
    from ..client import OwlBrowser


# Parameter aliases for flow JSON compatibility.
# Maps tool_name -> {alias_name -> canonical_name}
# This allows flow files to use shorthand parameter names that differ
# from the official API parameter names.
PARAMETER_ALIASES: Final[dict[str, dict[str, str]]] = {
    "browser_wait": {
        "ms": "timeout",
    },
    "browser_set_proxy": {
        "proxy_type": "type",
    },
}

# Tools where 'description' is a tool parameter, not just a step comment.
# For these tools, we should NOT strip 'description' from the params.
TOOLS_WITH_DESCRIPTION_PARAM: Final[frozenset[str]] = frozenset({
    "browser_find_element",
    "browser_ai_click",
    "browser_ai_type",
})


def _apply_parameter_aliases(tool_name: str, params: dict[str, Any]) -> dict[str, Any]:
    """
    Apply parameter aliases to convert flow shorthand names to API names.

    Args:
        tool_name: The tool being executed.
        params: Original parameters from the flow.

    Returns:
        Parameters with aliases resolved to canonical names.
    """
    aliases = PARAMETER_ALIASES.get(tool_name)
    if not aliases:
        return params

    result = dict(params)
    for alias, canonical in aliases.items():
        if alias in result and canonical not in result:
            result[canonical] = result.pop(alias)
    return result


class FlowExecutor:
    """
    Flow execution engine that runs a series of browser tool steps.

    Supports:
    - Variable resolution using ${prev} syntax
    - Expectation validation for result checking
    - Conditional branching with if/else logic
    - Automatic context_id injection

    Example:
        ```python
        from owl_browser import OwlBrowser, RemoteConfig
        from owl_browser.flow import FlowExecutor

        async with OwlBrowser(RemoteConfig(...)) as browser:
            ctx = await browser.create_context()
            executor = FlowExecutor(browser, ctx["context_id"])

            # Load and execute a flow
            flow = FlowExecutor.load_flow("test-flows/navigation.json")
            result = await executor.execute(flow)

            if result.success:
                print("Flow completed successfully!")
            else:
                print(f"Flow failed: {result.error}")
        ```
    """

    __slots__ = ("_client", "_context_id", "_abort_flag")

    def __init__(self, client: OwlBrowser, context_id: str) -> None:
        """
        Initialize flow executor.

        Args:
            client: OwlBrowser client instance.
            context_id: Browser context ID to use for execution.
        """
        self._client = client
        self._context_id = context_id
        self._abort_flag = False

    def abort(self) -> None:
        """Signal to abort the current flow execution."""
        self._abort_flag = True

    def reset(self) -> None:
        """Reset the abort flag for a new execution."""
        self._abort_flag = False

    async def execute(self, flow: Flow) -> FlowResult:
        """
        Execute a flow and return the results.

        Args:
            flow: The flow to execute.

        Returns:
            FlowResult containing all step results and overall status.
        """
        self._abort_flag = False
        start_time = time.monotonic()
        results: list[StepResult] = []

        enabled_steps = [s for s in flow.steps if s.enabled]
        if not enabled_steps:
            return FlowResult(
                success=True,
                steps=[],
                total_duration_ms=0,
            )

        try:
            success, _ = await self._execute_steps(enabled_steps, results, None)
        except FlowExecutionError as e:
            return FlowResult(
                success=False,
                steps=results,
                total_duration_ms=(time.monotonic() - start_time) * 1000,
                error=str(e),
            )
        except Exception as e:
            return FlowResult(
                success=False,
                steps=results,
                total_duration_ms=(time.monotonic() - start_time) * 1000,
                error=f"Unexpected error: {e}",
            )

        return FlowResult(
            success=success,
            steps=results,
            total_duration_ms=(time.monotonic() - start_time) * 1000,
        )

    async def _execute_steps(
        self,
        steps: list[FlowStep],
        results: list[StepResult],
        previous_result: Any,
    ) -> tuple[bool, Any]:
        """
        Execute a list of steps recursively.

        Returns:
            Tuple of (success, last_result).
        """
        last_result = previous_result

        for i, step in enumerate(steps):
            if self._abort_flag:
                return False, last_result

            if step.type == "condition" and step.condition:
                result = await self._execute_condition_step(
                    step, i, last_result, results
                )
                if not result.success:
                    return False, last_result
                last_result = result.result
            else:
                result = await self._execute_tool_step(step, i, last_result)
                results.append(result)

                if not result.success:
                    return False, last_result

                last_result = result.result

            if i < len(steps) - 1:
                await asyncio.sleep(0.1)

        return True, last_result

    async def _execute_condition_step(
        self,
        step: FlowStep,
        step_index: int,
        previous_result: Any,
        results: list[StepResult],
    ) -> StepResult:
        """Execute a condition step and its branches."""
        start_time = time.monotonic()

        if not step.condition:
            return StepResult(
                step_index=step_index,
                step_id=step.id,
                tool_name="condition",
                success=False,
                error="Condition step missing condition",
                duration_ms=(time.monotonic() - start_time) * 1000,
            )

        condition_result = evaluate_condition(step.condition, previous_result)
        branch_taken: Literal["true", "false"] = "true" if condition_result else "false"

        condition_step_result = StepResult(
            step_index=step_index,
            step_id=step.id,
            tool_name="condition",
            success=True,
            result={"condition_result": condition_result, "branch_taken": branch_taken},
            duration_ms=(time.monotonic() - start_time) * 1000,
            branch_taken=branch_taken,
        )
        results.append(condition_step_result)

        branch_steps = step.on_true if condition_result else step.on_false
        if branch_steps:
            success, last_result = await self._execute_steps(
                [s for s in branch_steps if s.enabled],
                results,
                previous_result,
            )
            if not success:
                return StepResult(
                    step_index=step_index,
                    step_id=step.id,
                    tool_name="condition",
                    success=False,
                    result=last_result,
                    error="Branch execution failed",
                    duration_ms=(time.monotonic() - start_time) * 1000,
                    branch_taken=branch_taken,
                )
            return StepResult(
                step_index=step_index,
                step_id=step.id,
                tool_name="condition",
                success=True,
                result=last_result,
                duration_ms=(time.monotonic() - start_time) * 1000,
                branch_taken=branch_taken,
            )

        return condition_step_result

    async def _execute_tool_step(
        self,
        step: FlowStep,
        step_index: int,
        previous_result: Any,
    ) -> StepResult:
        """Execute a single tool step."""
        start_time = time.monotonic()

        params = resolve_variables(step.params, previous_result)
        params = _apply_parameter_aliases(step.type, params)
        params["context_id"] = self._context_id

        try:
            result = await self._client.execute(step.type, **params)
            duration_ms = (time.monotonic() - start_time) * 1000

            if step.expected:
                expectation_result = check_expectation(result, step.expected)
                if not expectation_result.passed:
                    return StepResult(
                        step_index=step_index,
                        step_id=step.id,
                        tool_name=step.type,
                        success=False,
                        result=result,
                        error=f"Expectation failed: {expectation_result.message}",
                        duration_ms=duration_ms,
                        expectation_result=expectation_result,
                    )
                return StepResult(
                    step_index=step_index,
                    step_id=step.id,
                    tool_name=step.type,
                    success=True,
                    result=result,
                    duration_ms=duration_ms,
                    expectation_result=expectation_result,
                )

            return StepResult(
                step_index=step_index,
                step_id=step.id,
                tool_name=step.type,
                success=True,
                result=result,
                duration_ms=duration_ms,
            )

        except Exception as e:
            return StepResult(
                step_index=step_index,
                step_id=step.id,
                tool_name=step.type,
                success=False,
                error=str(e),
                duration_ms=(time.monotonic() - start_time) * 1000,
            )

    @staticmethod
    def load_flow(path: str | Path) -> Flow:
        """
        Load a flow from a JSON file.

        The JSON format matches the test-flows/*.json format used by
        the Owl Browser frontend.

        Args:
            path: Path to the JSON file.

        Returns:
            Parsed Flow object.

        Example:
            ```python
            flow = FlowExecutor.load_flow("test-flows/navigation.json")
            print(flow.name)  # "Navigation Tests"
            print(len(flow.steps))  # Number of steps
            ```
        """
        file_path = Path(path)
        content = file_path.read_text(encoding="utf-8")
        data = json.loads(content)
        return FlowExecutor._parse_flow(data)

    @staticmethod
    def _parse_flow(data: dict[str, Any]) -> Flow:
        """Parse flow data from dictionary."""
        steps = [FlowExecutor._parse_step(s) for s in data.get("steps", [])]
        return Flow(
            name=data.get("name", "Unnamed Flow"),
            description=data.get("description"),
            steps=steps,
        )

    @staticmethod
    def _parse_step(data: dict[str, Any]) -> FlowStep:
        """Parse a single step from dictionary."""
        step_id = data.get("id") or f"step_{uuid.uuid4().hex[:8]}"
        step_type = data.get("type", "")

        params = dict(data)
        # Remove metadata fields from params, keeping only tool parameters.
        # Note: 'description' is conditionally kept for tools that use it as a parameter.
        keys_to_remove = ["type", "selected", "enabled", "expected",
                         "condition", "onTrue", "onFalse", "on_true", "on_false", "id"]

        # Only remove 'description' if this tool doesn't use it as a parameter
        if step_type not in TOOLS_WITH_DESCRIPTION_PARAM:
            keys_to_remove.append("description")

        for key in keys_to_remove:
            params.pop(key, None)

        expected = None
        if "expected" in data:
            exp_data = data["expected"]
            expected = StepExpectation(
                equals=exp_data.get("equals"),
                contains=exp_data.get("contains"),
                length=exp_data.get("length"),
                greater_than=exp_data.get("greaterThan") or exp_data.get("greater_than"),
                less_than=exp_data.get("lessThan") or exp_data.get("less_than"),
                not_empty=exp_data.get("notEmpty") or exp_data.get("not_empty"),
                field=exp_data.get("field"),
                matches=exp_data.get("matches"),
            )

        condition = None
        if "condition" in data:
            cond_data = data["condition"]
            from ..types import ConditionOperator
            condition = FlowCondition(
                source=cond_data.get("source", "previous"),
                operator=ConditionOperator(cond_data.get("operator", "is_truthy")),
                source_step_id=cond_data.get("sourceStepId") or cond_data.get("source_step_id"),
                field=cond_data.get("field"),
                value=cond_data.get("value"),
            )

        on_true = None
        on_false = None
        if "onTrue" in data or "on_true" in data:
            on_true_data = data.get("onTrue") or data.get("on_true") or []
            on_true = [FlowExecutor._parse_step(s) for s in on_true_data]
        if "onFalse" in data or "on_false" in data:
            on_false_data = data.get("onFalse") or data.get("on_false") or []
            on_false = [FlowExecutor._parse_step(s) for s in on_false_data]

        enabled = data.get("enabled", data.get("selected", True))

        return FlowStep(
            id=step_id,
            type=data.get("type", ""),
            enabled=enabled,
            params=params,
            description=data.get("description"),
            expected=expected,
            condition=condition,
            on_true=on_true,
            on_false=on_false,
        )
