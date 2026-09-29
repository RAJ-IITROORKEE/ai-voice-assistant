import { createClient } from "@insforge/sdk";

// Browser/anon client. Keys come from .env.local (never commit it).
// NEXT_PUBLIC_* is safe here because the anon key is RLS-gated.
export const insforge = createClient({
  baseUrl:
    process.env.NEXT_PUBLIC_INSFORGE_BASE_URL ??
    "https://ts4hxi45.us-east.insforge.app",
  anonKey: process.env.NEXT_PUBLIC_INSFORGE_ANON_KEY ?? "",
});
