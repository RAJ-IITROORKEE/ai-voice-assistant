-- Luna Voice Agent — initial application schema
-- All tables are user-scoped via RLS. The auth JWT carries the user id in `sub`.
-- Accessor: current_setting('request.jwt.claims', true)::jsonb ->> 'sub'

-- ============ DEVICES ============
create table if not exists public.devices (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  name text not null default 'Luna Device',
  device_id text not null,            -- MAC / chip id from firmware
  firmware text,
  last_seen timestamptz,
  online boolean not null default false,
  created_at timestamptz not null default now(),
  unique (user_id, device_id)
);

-- ============ CONVERSATIONS ============
create table if not exists public.conversations (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  device_id uuid references public.devices(id) on delete set null,
  title text not null default 'New conversation',
  source text not null default 'web', -- 'web' | 'device'
  pinned boolean not null default false,
  archived boolean not null default false,
  deleted boolean not null default false,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

-- ============ MESSAGES ============
create table if not exists public.messages (
  id uuid primary key default gen_random_uuid(),
  conversation_id uuid not null references public.conversations(id) on delete cascade,
  user_id uuid not null references auth.users(id) on delete cascade,
  role text not null,                 -- 'user' | 'assistant' | 'system' | 'tool'
  content text not null default '',
  audio_url text,                     -- optional spoken audio reference
  metadata jsonb not null default '{}'::jsonb,  -- pipeline, latency, model, tool calls
  created_at timestamptz not null default now()
);

-- ============ SETTINGS (per user) ============
create table if not exists public.settings (
  user_id uuid primary key references auth.users(id) on delete cascade,
  pipeline text not null default 'classic',     -- classic | azure-realtime | gemini-live
  model text not null default 'gpt-5.6-luna',
  voice text not null default 'en-IN-NeerjaNeural',
  language text not null default 'en-IN',
  persona text not null default 'You are Luna, a concise, friendly voice assistant. Answer briefly and clearly; responses are spoken aloud.',
  theme text not null default 'dark',
  extra jsonb not null default '{}'::jsonb,
  updated_at timestamptz not null default now()
);

-- ============ MCP SERVERS (per user) ============
create table if not exists public.mcp_servers (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  name text not null,
  url text not null,
  transport text not null default 'http',  -- http | stdio
  auth_token text,                          -- encrypted at rest by InsForge secrets later
  enabled boolean not null default true,
  read_only boolean not null default true,  -- guardrail: read-only by default
  allowed_tools text[] not null default '{}',
  status text not null default 'unknown',   -- unknown | ok | error
  created_at timestamptz not null default now(),
  unique (user_id, name)
);

-- ============ TOOL CALLS (audit log) ============
create table if not exists public.tool_calls (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  conversation_id uuid references public.conversations(id) on delete set null,
  server text,                  -- mcp server name or 'built-in'
  tool text not null,
  args jsonb not null default '{}'::jsonb,
  result_summary text,
  approved boolean,             -- null = not required, true/false = human decision
  status text not null default 'pending', -- pending | approved | denied | ran | error
  created_at timestamptz not null default now()
);

-- ============ INDEXES ============
create index if not exists devices_user_idx on public.devices(user_id);
create index if not exists conversations_user_idx on public.conversations(user_id, deleted, archived, pinned, updated_at desc);
create index if not exists messages_conversation_idx on public.messages(conversation_id, created_at);
create index if not exists messages_user_idx on public.messages(user_id);
create index if not exists mcp_servers_user_idx on public.mcp_servers(user_id);
create index if not exists tool_calls_user_idx on public.tool_calls(user_id, created_at desc);

-- ============ ENABLE RLS ============
alter table public.devices enable row level security;
alter table public.conversations enable row level security;
alter table public.messages enable row level security;
alter table public.settings enable row level security;
alter table public.mcp_servers enable row level security;
alter table public.tool_calls enable row level security;

-- ============ RLS POLICIES (owner-only rows) ============
-- Helper expression: (current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid

create policy devices_owner on public.devices
  for all using ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id)
  with check ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id);

create policy conversations_owner on public.conversations
  for all using ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id)
  with check ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id);

create policy messages_owner on public.messages
  for all using ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id)
  with check ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id);

create policy settings_owner on public.settings
  for all using ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id)
  with check ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id);

create policy mcp_servers_owner on public.mcp_servers
  for all using ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id)
  with check ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id);

create policy tool_calls_owner on public.tool_calls
  for all using ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id)
  with check ((current_setting('request.jwt.claims', true)::jsonb ->> 'sub')::uuid = user_id);

-- ============ GRANTS to API roles ============
grant select, insert, update, delete on public.devices to authenticated, anon;
grant select, insert, update, delete on public.conversations to authenticated, anon;
grant select, insert, update, delete on public.messages to authenticated, anon;
grant select, insert, update, delete on public.settings to authenticated, anon;
grant select, insert, update, delete on public.mcp_servers to authenticated, anon;
grant select, insert, update, delete on public.tool_calls to authenticated, anon;

-- ============ updated_at trigger ============
create or replace function public.set_updated_at() returns trigger as $$
begin
  new.updated_at = now();
  return new;
end; $$ language plpgsql;

drop trigger if exists conversations_updated on public.conversations;
create trigger conversations_updated before update on public.conversations
  for each row execute function public.set_updated_at();

drop trigger if exists settings_updated on public.settings;
create trigger settings_updated before update on public.settings
  for each row execute function public.set_updated_at();
