"use client";

import * as React from "react";
import { useRouter } from "next/navigation";
import { insforge } from "@/lib/insforge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";

export default function SignInPage() {
  const router = useRouter();
  const [email, setEmail] = React.useState("");
  const [password, setPassword] = React.useState("");
  const [mode, setMode] = React.useState<"signin" | "signup">("signin");
  const [error, setError] = React.useState<string | null>(null);
  const [busy, setBusy] = React.useState(false);

  // If already signed in, go home.
  React.useEffect(() => {
    insforge.auth.getCurrentUser().then(({ data }) => {
      if (data?.user) router.replace("/");
    });
  }, [router]);

  async function handleEmail(e: React.FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      if (mode === "signin") {
        const { error } = await insforge.auth.signInWithPassword({ email, password });
        if (error) throw error;
        router.replace("/");
      } else {
        const { data, error } = await insforge.auth.signUp({ email, password });
        if (error) throw error;
        if (data?.accessToken) router.replace("/");
        else setError("Account created. Please sign in.");
      }
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : "Authentication failed");
    } finally {
      setBusy(false);
    }
  }

  async function handleOAuth(provider: "google" | "github") {
    setBusy(true);
    setError(null);
    try {
      await insforge.auth.signInWithOAuth(provider, {
        redirectTo: window.location.origin + "/",
      });
      // browser redirects away
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : "OAuth failed");
      setBusy(false);
    }
  }

  return (
    <div className="bg-background flex min-h-dvh items-center justify-center p-4">
      <Card className="w-full max-w-sm">
        <CardHeader>
          <CardTitle className="text-xl">Luna Voice Agent</CardTitle>
          <CardDescription>
            {mode === "signin" ? "Sign in to continue" : "Create your account"}
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-4">
          <div className="grid grid-cols-2 gap-2">
            <Button variant="outline" onClick={() => handleOAuth("google")} disabled={busy}>
              Google
            </Button>
            <Button variant="outline" onClick={() => handleOAuth("github")} disabled={busy}>
              GitHub
            </Button>
          </div>

          <div className="relative">
            <div className="absolute inset-0 flex items-center">
              <span className="w-full border-t" />
            </div>
            <div className="relative flex justify-center text-xs uppercase">
              <span className="bg-card text-muted-foreground px-2">or</span>
            </div>
          </div>

          <form onSubmit={handleEmail} className="flex flex-col gap-3">
            <div className="grid gap-2">
              <Label htmlFor="email">Email</Label>
              <Input
                id="email"
                type="email"
                required
                autoComplete="email"
                value={email}
                onChange={(e) => setEmail(e.target.value)}
                placeholder="you@example.com"
              />
            </div>
            <div className="grid gap-2">
              <Label htmlFor="password">Password</Label>
              <Input
                id="password"
                type="password"
                required
                autoComplete={mode === "signin" ? "current-password" : "new-password"}
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                placeholder="••••••••"
              />
            </div>
            {error && <p className="text-destructive text-sm">{error}</p>}
            <Button type="submit" disabled={busy}>
              {busy ? "Please wait…" : mode === "signin" ? "Sign in" : "Sign up"}
            </Button>
          </form>

          <button
            type="button"
            onClick={() => setMode(mode === "signin" ? "signup" : "signin")}
            className="text-muted-foreground text-sm underline-offset-4 hover:underline"
          >
            {mode === "signin"
              ? "No account? Sign up"
              : "Already have an account? Sign in"}
          </button>
        </CardContent>
      </Card>
    </div>
  );
}
