from __future__ import annotations

import ast
import operator
from datetime import datetime, timezone
from typing import Any

from langchain_core.tools import tool


@tool
def get_current_time() -> str:
    """Return the current UTC date and time (ISO 8601). Use when the user asks for the
    current time or date."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


_BINOPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.truediv,
    ast.Mod: operator.mod,
    ast.FloorDiv: operator.floordiv,
    ast.Pow: operator.pow,
}


def _eval(node: Any) -> float:
    if isinstance(node, ast.Expression):
        return _eval(node.body)
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return float(node.value)
    if isinstance(node, ast.BinOp):
        op = _BINOPS.get(type(node.op))
        if op is None:
            raise ValueError("operator not allowed")
        return op(_eval(node.left), _eval(node.right))
    if isinstance(node, ast.UnaryOp):
        val = _eval(node.operand)
        if isinstance(node.op, ast.UAdd):
            return val
        if isinstance(node.op, ast.USub):
            return -val
    raise ValueError("expression not allowed")


@tool
def calculator(expression: str) -> str:
    """Evaluate a basic arithmetic expression (numbers, + - * / % // ** and parentheses).
    Use for any math the user asks you to compute. Example: '2 * (3 + 4)'."""
    try:
        result = _eval(ast.parse(expression, mode="eval"))
    except Exception as e:
        return f"Could not evaluate '{expression}': {e}"
    return str(int(result)) if result == int(result) else f"{result:.6g}"


# Tools exposed to the agent. Web search joins this list once a search API key is configured.
BUILTIN_TOOLS = [get_current_time, calculator]
