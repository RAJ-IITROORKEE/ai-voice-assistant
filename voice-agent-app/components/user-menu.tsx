"use client";

import * as React from "react";
import { useRouter } from "next/navigation";
import { insforge } from "@/lib/insforge";
import { useRequireAuth } from "@/lib/use-auth";
import { Avatar, AvatarFallback, AvatarImage } from "@/components/ui/avatar";
import { Button } from "@/components/ui/button";
import { LogOut } from "lucide-react";

export function UserMenu() {
  const router = useRouter();
  const { user } = useRequireAuth();
  if (!user) return null;

  const name = user.profile?.name ?? user.email;
  const initials = name
    .split(/\s+/)
    .map((p) => p[0])
    .join("")
    .slice(0, 2)
    .toUpperCase();

  async function signOut() {
    await insforge.auth.signOut();
    router.replace("/sign-in");
  }

  return (
    <div className="flex min-w-0 items-center gap-2">
      <Avatar className="size-7">
        <AvatarImage src={user.profile?.avatar_url ?? undefined} alt={name} />
        <AvatarFallback className="text-xs">{initials}</AvatarFallback>
      </Avatar>
      <span className="text-muted-foreground max-w-[110px] truncate text-xs">
        {name}
      </span>
      <Button variant="ghost" size="icon" aria-label="Sign out" onClick={signOut}>
        <LogOut className="size-4" />
      </Button>
    </div>
  );
}
