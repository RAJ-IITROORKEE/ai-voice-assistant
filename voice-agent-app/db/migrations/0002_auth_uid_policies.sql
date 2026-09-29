-- Simplify RLS policies to use the built-in auth.uid() helper.
-- auth.uid() returns the authenticated user's uuid (from JWT sub), or NULL for anon.

do $$
declare t text;
begin
  foreach t in array array['devices','conversations','messages','settings','mcp_servers','tool_calls']
  loop
    execute format('drop policy if exists %I on public.%I', t || '_owner', t);
    execute format(
      'create policy %I on public.%I for all using (auth.uid() = user_id) with check (auth.uid() = user_id)',
      t || '_owner', t
    );
  end loop;
end $$;

-- reload PostgREST so the policy change is picked up
notify pgrst, 'reload schema';
