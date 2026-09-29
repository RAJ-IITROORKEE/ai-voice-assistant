from __future__ import annotations

import asyncio
import json
import sys
import time
import uuid
from contextlib import asynccontextmanager
from typing import Any, Optional

# psycopg async needs a SelectorEventLoop on Windows (uvicorn's Proactor loop is
# incompatible). Set the policy at import time so every loop created afterwards is a selector loop.
if sys.platform.startswith("win"):
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import StreamingResponse
from pydantic import BaseModel

import db
from auth import validate_token
from config import settings
from graph import _to_lc, graph


@asynccontextmanager
async def lifespan(app: FastAPI):
    # psycopg async requires a SelectorEventLoop on Windows; uvicorn's default
    # Proactor loop is incompatible, so swap the policy before anything opens a conn.
    import asyncio
    import sys
    if sys.platform.startswith("win"):
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    yield
    await db.close_pool()


app = FastAPI(title="Luna Agent", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[o.strip() for o in settings.cors_origins.split(",")] or ["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


class ChatMessage(BaseModel):
    role: str
    content: Any  # str or list of content parts


class ChatRequest(BaseModel):
    model: Optional[str] = None
    messages: list[ChatMessage]
    stream: bool = True
    thread_id: Optional[str] = None
    user_sub: Optional[str] = None  # set by relay (device), trusted via device token
    source: Optional[str] = None  # 'device' for relay turns; defaults to 'web'


def _flatten(content: Any) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = []
        for c in content:
            if isinstance(c, dict):
                if c.get("type") in ("text", "input_text") and c.get("text"):
                    parts.append(c["text"])
                elif c.get("type") == "output_text" and c.get("text"):
                    parts.append(c["text"])
        return "".join(parts)
    return ""


async def get_principal(
    authorization: Optional[str] = Header(default=None),
    x_device_token: Optional[str] = Header(default=None),
    x_device_id: Optional[str] = Header(default=None),
    body: Optional[ChatRequest] = None,
) -> str:
    """Return user_sub. Accept an InsForge JWT, or the relay device token + user_sub."""
    if authorization and authorization.lower().startswith("bearer "):
        token = authorization.split(None, 1)[1]
        claims = await validate_token(token)
        if claims and claims.get("sub"):
            return claims["sub"]
        raise HTTPException(status_code=401, detail="invalid token")
    # Device path (relay): fall back to relay-authenticated user_sub if provided.
    if body and body.user_sub:
        return body.user_sub
    raise HTTPException(status_code=401, detail="missing authorization")


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.get("/ready")
async def ready():
    return {"status": "ready"}


@app.post("/v1/chat/completions")
async def chat_completions(body: ChatRequest, user_sub: str = Depends(get_principal)):
    if not body.messages:
        raise HTTPException(status_code=400, detail="messages required")

    source = "device" if (body.source or "").lower() == "device" else "web"
    thread_id = await db.get_or_create_conversation(user_sub, body.thread_id, source)

    history = await db.load_history(user_sub, thread_id)
    # Build model messages: system + prior history + current incoming user turn(s).
    lc_messages = []
    if settings.system_prompt:
        lc_messages.append(_to_lc("system", settings.system_prompt))
    for m in history:
        lc_messages.append(_to_lc(m["role"], m["content"]))
    for m in body.messages:
        text = _flatten(m.content)
        if m.role == "system":
            continue  # system prompt is owned by the agent
        if not text:
            continue
        lc_messages.append(_to_lc(m.role, text))

    last_user_text = next(
        (t for t in (_flatten(m.content) for m in reversed(body.messages)) if t), ""
    )

    completion_id = f"chatcmpl-{uuid.uuid4().hex[:24]}"
    created = int(time.time())
    model_name = body.model or settings.azure_openai_model

    initial_state = {
        "messages": lc_messages,
        "user_sub": user_sub,
        "thread_id": thread_id,
    }

    async def sse_stream():
        full: list[str] = []
        # Persist the user turn.
        for m in body.messages:
            text = _flatten(m.content)
            if text and m.role == "user":
                await db.append_message(user_sub, thread_id, "user", text)
        if last_user_text:
            await db.maybe_set_title(user_sub, thread_id, last_user_text)

        # role chunk
        yield "data: " + json.dumps({
            "id": completion_id, "object": "chat.completion.chunk", "created": created,
            "model": model_name,
            "choices": [{"index": 0, "delta": {"role": "assistant"}, "finish_reason": None}],
        }) + "\n\n"
        try:
            # Run the LangGraph tool loop, streaming only assistant content tokens
            # (skip tool-call arguments and tool results).
            async for event in graph.astream_events(initial_state, version="v2"):
                if event.get("event") != "on_chat_model_stream":
                    continue
                chunk = event.get("data", {}).get("chunk")
                token = getattr(chunk, "content", None) if chunk is not None else None
                if not token:
                    continue
                full.append(token)
                yield "data: " + json.dumps({
                    "id": completion_id, "object": "chat.completion.chunk", "created": created,
                    "model": model_name,
                    "choices": [{"index": 0, "delta": {"content": token}, "finish_reason": None}],
                }) + "\n\n"
            yield "data: " + json.dumps({
                "id": completion_id, "object": "chat.completion.chunk", "created": created,
                "model": model_name,
                "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
            }) + "\n\n"
            yield "data: [DONE]\n\n"
            text = "".join(full)
            if text:
                await db.append_message(user_sub, thread_id, "assistant", text)
        except Exception as e:  # pragma: no cover
            yield "data: " + json.dumps({"error": str(e)}) + "\n\n"
            yield "data: [DONE]\n\n"

    return StreamingResponse(sse_stream(), media_type="text/event-stream")


if __name__ == "__main__":
    # Run uvicorn programmatically with loop="none" so asyncio.run() defers to
    # the event-loop POLICY set at module import time (WindowsSelectorEventLoopPolicy
    # on Windows). Uvicorn's default "asyncio" loop factory explicitly returns
    # ProactorEventLoop on win32, which psycopg-async cannot use.
    import os
    import sys
    import uvicorn

    kwargs: dict = {}
    if sys.platform.startswith("win"):
        kwargs["loop"] = "none"

    uvicorn.run(
        "main:app",
        host=os.environ.get("HOST", "0.0.0.0"),
        port=int(os.environ.get("PORT", "8000")),
        reload=False,
        **kwargs,
    )
