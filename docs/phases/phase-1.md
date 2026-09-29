# Phase 1 — Web App MVP: Completion Report

**Status:** ✅ Gate passed
**Date:** 2026-09-29
**Commit:** `9a5e607` (+ follow-ups) · **Tag:** `v1.1`
**Live URL:** https://luna-voice-agent.vercel.app

## What was built

### Web app (`voice-agent-app/`)
- **Stack:** Next.js 16.3.6 (App Router, Turbopack), React 19, Tailwind v4, shadcn/ui (base-nova), assistant-ui, AI SDK v7, `@insforge/sdk`.
- **Shell:** `AppShell` — ChatGPT/Claude-style layout. Sidebar: Chats (thread list), Devices, Tools, MCP Servers, Settings. Header with sidebar trigger; footer with user menu + theme toggle. Dark-first (next-themes) with light mode; fully responsive.
- **Panels:** Devices, Tools, MCP, Settings (placeholders wired to InsForge next phase).
- **Chat:** assistant-ui `Thread` + `ThreadList`; `/api/chat` streams from Azure Foundry OpenAI-compatible endpoint (`gpt-5.6-luna`).
- **Auth:** `/sign-in` page (email/password + Google/GitHub OAuth), `AuthGate` guard on all protected pages, `useRequireAuth` hook, `UserMenu` (avatar + sign-out).

### Backend (InsForge project `luna-voice-agent`, us-east)
- Project id `dbe5bdcc-163a-473f-92ab-924bd784044e`, appkey `ts4hxi45`, base `https://ts4hxi45.us-east.insforge.app`.
- OAuth providers: Google + GitHub (enabled by default). `require_email_verification=false` (no SMTP on free tier). Allowed redirect URLs: prod Vercel + localhost.
- **Schema** (`db/migrations/0001_init.sql`, `0002_auth_uid_policies.sql`): tables `devices, conversations, messages, settings, mcp_servers, tool_calls`; indexes; `set_updated_at()` triggers; grants to anon+authenticated; owner-only RLS via `auth.uid()`.
- Admin user `rajrabidas001@gmail.com` (id `d535784d-669c-4a6e-931b-49c9580d2a44`, Google-linked).

## Gate verification (all passed)
- `pnpm build` — all 8 routes compile (incl. `/sign-in`).
- Dev server: `/`, `/sign-in`, `/devices`, `/tools`, `/mcp`, `/settings` → 200; sign-in renders.
- `/api/chat` streams real tokens from Azure.
- **RLS E2E via REST:** owner JWT read/insert ✓, anon → `[]` ✓, admin `ik_` bypass ✓.
- **Production deploy:** https://luna-voice-agent.vercel.app/sign-in → 200, renders.

## Key learnings / gotchas
- After creating tables, run `NOTIFY pgrst, 'reload schema'` or PostgREST returns `{}` on inserts.
- No local `psql` — migrations applied via `scripts/run-migration.js` (node `pg` client).
- RLS accessor: `auth.uid()` (built-in helper reading JWT `sub`).
- InsForge CLI auth via device flow (RFC 8628) — works headless; token in this session only.

## Next phase
Phase 2 — LangGraph orchestration service (`agent/`, Python, Docker → ACA `luna-agent`), route web chat through it.
