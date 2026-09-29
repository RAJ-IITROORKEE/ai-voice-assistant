"use client";

import * as React from "react";
import { useRequireAuth } from "@/lib/use-auth";
import { Loader2 } from "lucide-react";

/** Wraps protected pages: shows a loader while checking session, redirects to /sign-in if unauthenticated. */
export function AuthGate({ children }: { children: React.ReactNode }) {
  const { user, loading } = useRequireAuth();

  if (loading || !user) {
    return (
      <div className="bg-background flex min-h-dvh items-center justify-center">
        <Loader2 className="text-muted-foreground size-6 animate-spin" />
      </div>
    );
  }
  return <>{children}</>;
}
