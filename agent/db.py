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


async def get_or_create_conversation(
    user_sub: str, thread_id: Optional[str], source: str = "web"
) -> str:
    thread_uuid = _thread_uuid(user_sub, thread_id)
    pool = await get_pool()
    async with pool.connection() as conn:
        await conn.execute(
            "SELECT set_config('request.jwt.claims', %s, true)", (_claims(user_sub),)
        )
        await conn.execute(
            """
            INSERT INTO conversations (id, user_id, title, source)
            VALUES (%s, %s, %s, %s)
            ON CONFLICT (id) DO NOTHING
            """,
            (str(thread_uuid), user_sub, "New chat", source),
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
