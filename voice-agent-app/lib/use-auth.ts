"use client";

import * as React from "react";
import { useRouter } from "next/navigation";
import { insforge } from "@/lib/insforge";

export type AuthUser = {
  id: string;
  email: string;
  profile?: { name?: string; avatar_url?: string };
};

/** Client-side session hook. Redirects to /sign-in when unauthenticated. */
export function useRequireAuth(redirectTo = "/sign-in") {
  const router = useRouter();
  const [user, setUser] = React.useState<AuthUser | null>(null);
  const [loading, setLoading] = React.useState(true);

  React.useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const { data } = await insforge.auth.getCurrentUser();
        if (!cancelled) {
          if (data?.user) {
            setUser(data.user as AuthUser);
          } else {
            router.replace(redirectTo);
          }
        }
      } catch {
        if (!cancelled) router.replace(redirectTo);
      } finally {
        if (!cancelled) setLoading(false);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [router, redirectTo]);

  return { user, loading, signOut: () => insforge.auth.signOut() };
}
