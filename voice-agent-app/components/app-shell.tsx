"use client";

import type * as React from "react";
import Link from "next/link";
import {
  Cpu,
  MessagesSquare,
  Plug,
  Settings,
  Wrench,
} from "lucide-react";
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarGroup,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
  SidebarInset,
  SidebarTrigger,
  SidebarRail,
} from "@/components/ui/sidebar";
import { Separator } from "@/components/ui/separator";
import { ModeToggle } from "@/components/mode-toggle";
import { UserMenu } from "@/components/user-menu";

export type NavSection = "chat" | "conversations" | "devices" | "tools" | "mcp" | "settings";

const NAV: { key: NavSection; label: string; href: string; icon: React.ElementType }[] = [
  { key: "chat", label: "Chats", href: "/", icon: MessagesSquare },
  { key: "conversations", label: "Conversations", href: "/conversations", icon: MessagesSquare },
  { key: "devices", label: "Devices", href: "/devices", icon: Cpu },
  { key: "tools", label: "Tools", href: "/tools", icon: Wrench },
  { key: "mcp", label: "MCP Servers", href: "/mcp", icon: Plug },
  { key: "settings", label: "Settings", href: "/settings", icon: Settings },
];

export function AppShell({
  active,
  children,
  sidebarExtra,
}: {
  active: NavSection;
  children: React.ReactNode;
  /** Rendered in the sidebar content above the nav (e.g. the thread list on the chat page). */
  sidebarExtra?: React.ReactNode;
}) {
  return (
    <SidebarProvider>
      <div className="flex h-dvh w-full">
        <Sidebar>
          <SidebarHeader className="border-b">
            <SidebarMenu>
              <SidebarMenuItem>
                <SidebarMenuButton size="lg" render={<Link href="/" />}>
                  <div className="bg-sidebar-primary text-sidebar-primary-foreground flex aspect-square size-8 items-center justify-center rounded-lg">
                    <MessagesSquare className="size-4" />
                  </div>
                  <div className="flex flex-col gap-0.5 leading-none">
                    <span className="font-semibold">Luna</span>
                    <span className="text-muted-foreground text-xs">Voice Agent</span>
                  </div>
                </SidebarMenuButton>
              </SidebarMenuItem>
            </SidebarMenu>
          </SidebarHeader>

          <SidebarContent className="px-2">
            {sidebarExtra}
            <SidebarGroup>
              <SidebarGroupLabel>Control</SidebarGroupLabel>
              <SidebarMenu>
                {NAV.filter((n) => n.key !== "chat").map((item) => (
                  <SidebarMenuItem key={item.key}>
                    <SidebarMenuButton
                      isActive={active === item.key}
                      render={<Link href={item.href} />}
                    >
                      <item.icon className="size-4" />
                      <span>{item.label}</span>
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                ))}
              </SidebarMenu>
            </SidebarGroup>
          </SidebarContent>

          <SidebarRail />
          <SidebarFooter className="border-t">
            <div className="flex items-center justify-between px-2 py-1">
              <UserMenu />
              <ModeToggle />
            </div>
          </SidebarFooter>
        </Sidebar>

        <SidebarInset>
          <header className="flex h-16 shrink-0 items-center gap-2 border-b px-4">
            <SidebarTrigger />
            <Separator orientation="vertical" className="mr-2 h-4" />
            <span className="font-medium capitalize">{active}</span>
          </header>
          <div className="flex-1 overflow-hidden">{children}</div>
        </SidebarInset>
      </div>
    </SidebarProvider>
  );
}
