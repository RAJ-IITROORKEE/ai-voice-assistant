from __future__ import annotations

import time
from typing import Any, Optional

import httpx
import jwt
from jwt.algorithms import RSAAlgorithm

from config import settings

_cache: dict[str, Any] = {"keys": {}, "fetched_at": 0.0}
_TTL = 3600.0


async def _jwks() -> dict[str, Any]:
    now = time.time()
    if _cache["keys"] and now - _cache["fetched_at"] < _TTL:
        return _cache["keys"]
    url = settings.insforge_base_url.rstrip("/") + "/.well-known/jwks.json"
    async with httpx.AsyncClient(timeout=10) as client:
        resp = await client.get(url)
        resp.raise_for_status()
        data = resp.json()
    keys = {k["kid"]: RSAAlgorithm.from_jwk(k) for k in data.get("keys", [])}
    _cache["keys"] = keys
    _cache["fetched_at"] = now
    return keys


async def validate_token(token: str) -> Optional[dict[str, Any]]:
    """Validate an InsForge RS256 access token. Returns claims or None."""
    try:
        header = jwt.get_unverified_header(token)
        keys = await _jwks()
        key = keys.get(header.get("kid"))
        if key is None:
            # Force refresh once in case of rotation.
            _cache["fetched_at"] = 0.0
            keys = await _jwks()
            key = keys.get(header.get("kid"))
            if key is None:
                return None
        claims = jwt.decode(
            token,
            key=key,
            algorithms=["RS256"],
            options={"verify_aud": False, "verify_iss": False},
        )
        return claims
    except Exception:
        return None
