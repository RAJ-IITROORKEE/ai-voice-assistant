from __future__ import annotations

import json
import uuid
from typing import Any, Optional

from psycopg_pool import AsyncConnectionPool

from config import settings

_pool: Optional[AsyncConnectionPool] = None


async def get_pool() -> AsyncConnectionPool:
    global _pool
    if _pool is None:
        _pool = AsyncConnectionPool(
            conninfo=settings.database_url,
            min_size=1,
            max_size=5,
            open=False,
        )
        await _pool.open()
    return _pool


async def close_pool() -> None:
    global _pool
    if _pool is not None:
        await _pool.close()
        _pool = None


def _claims(user_sub: str) -> str:
    return json.dumps({"sub": user_sub})


def _thread_uuid(user_sub: str, thread_id: Optional[str]) -> uuid.UUID:
    tid = thread_id or str(uuid.uuid4())
    try:
        return uuid.UUID(tid)
    except (ValueError, AttributeError):
        return uuid.uuid5(uuid.NAMESPACE_URL, f"thread:{user_sub}:{tid}")


async def _resolve_device_uuid(user_sub: str, thread_id: Optional[str]) -> Optional[str]:
    """Map a relay thread_id like 'device-esp32-b43a45bd1d9c' to the devices.id row (uuid),
    so device conversations are linked to the device that produced them. None for web turns."""
    if not thread_id or not thread_id.startswith("device-"):
        return None
    device_key = thread_id[len("device-"):]
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        cur = await conn.execute(
            "SELECT id FROM devices WHERE device_id = %s LIMIT 1", (device_key,)
        )
        row = await cur.fetchone()
    return str(row[0]) if row else None


async def get_or_create_conversation(
    user_sub: str, thread_id: Optional[str], source: str = "web"
) -> str:
    thread_uuid = _thread_uuid(user_sub, thread_id)
    device_uuid = await _resolve_device_uuid(user_sub, thread_id) if source == "device" else None
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        await conn.execute(
            """
            INSERT INTO conversations (id, user_id, title, source, device_id)
            VALUES (%s, %s, %s, %s, %s)
            ON CONFLICT (id) DO UPDATE
              SET device_id = COALESCE(conversations.device_id, EXCLUDED.device_id)
            """,
            (str(thread_uuid), user_sub, "New chat", source, device_uuid),
        )
    return str(thread_uuid)


async def load_history(user_sub: str, thread_id: str) -> list[dict[str, Any]]:
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        cur = await conn.execute(
            "SELECT role, content FROM messages WHERE conversation_id = %s ORDER BY created_at ASC LIMIT %s",
            (thread_id, settings.max_history_messages),
        )
        rows = await cur.fetchall()
    return [{"role": r[0], "content": r[1]} for r in rows]


async def append_message(user_sub: str, thread_id: str, role: str, content: str) -> None:
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        await conn.execute(
            "INSERT INTO messages (conversation_id, user_id, role, content) VALUES (%s, %s, %s, %s)",
            (thread_id, user_sub, role, content),
        )


async def record_tool_call(
    user_sub: str,
    thread_id: str,
    tool: str,
    args: dict[str, Any],
    result_summary: str,
    status: str,
    server: str = "built-in",
) -> None:
    """Persist a tool invocation to tool_calls so the web UI can show what the agent did."""
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        await conn.execute(
            """
            INSERT INTO tool_calls (user_id, conversation_id, server, tool, args, result_summary, approved, status)
            VALUES (%s, %s, %s, %s, %s::jsonb, %s, %s, %s)
            """,
            (user_sub, thread_id, server, tool, json.dumps(args),
             result_summary[:2000], True, status),
        )


async def add_reminder(
    user_sub: str, task: str, remind_at: Optional[str], note: Optional[str]
) -> str:
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        cur = await conn.execute(
            "INSERT INTO reminders (user_id, task, remind_at, note) VALUES (%s, %s, %s, %s) RETURNING id",
            (user_sub, task, remind_at, note),
        )
        row = await cur.fetchone()
    return str(row[0])


async def list_reminders(user_sub: str, include_done: bool = False) -> list[dict[str, Any]]:
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        cur = await conn.execute(
            """
            SELECT task, remind_at, note, done FROM reminders
            WHERE user_id = %s AND (%s OR done = false)
            ORDER BY done ASC, remind_at ASC NULLS LAST, created_at DESC LIMIT 20
            """,
            (user_sub, include_done),
        )
        rows = await cur.fetchall()
    return [{"task": r[0], "remind_at": str(r[1]) if r[1] else None, "note": r[2], "done": r[3]} for r in rows]


async def save_note(
    user_sub: str, title: str, body: Optional[str], kind: str = "note", tags: Optional[list[str]] = None
) -> str:
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        cur = await conn.execute(
            "INSERT INTO notes (user_id, kind, title, body, tags) VALUES (%s, %s, %s, %s, %s) RETURNING id",
            (user_sub, kind, title, body, tags),
        )
        row = await cur.fetchone()
    return str(row[0])


async def list_notes(user_sub: str, kind: Optional[str] = None, open_only: bool = False) -> list[dict[str, Any]]:
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        cur = await conn.execute(
            """
            SELECT kind, title, body, tags, done FROM notes
            WHERE user_id = %s AND (%s IS NULL OR kind = %s) AND (%s OR done = false)
            ORDER BY created_at DESC LIMIT 20
            """,
            (user_sub, kind, kind, open_only),
        )
        rows = await cur.fetchall()
    return [
        {"kind": r[0], "title": r[1], "body": r[2], "tags": r[3], "done": r[4]} for r in rows
    ]


async def maybe_set_title(user_sub: str, thread_id: str, first_user_text: str) -> None:
    title = first_user_text.strip().splitlines()[0][:80] if first_user_text.strip() else "New chat"
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        await conn.execute(
            "UPDATE conversations SET title = %s WHERE id = %s AND (title IS NULL OR title = 'New conversation' OR title = 'New chat')",
            (title, thread_id),
        )
