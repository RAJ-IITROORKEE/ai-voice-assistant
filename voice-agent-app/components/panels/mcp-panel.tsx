"use client";

import { Plug, Plus } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";

export function McpPanel() {
  return (
    <div className="mx-auto flex h-full max-w-3xl flex-col gap-4 overflow-y-auto p-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold">MCP servers</h2>
          <p className="text-muted-foreground text-sm">
            Connect Model Context Protocol servers (Notion, Gmail, …) to give the agent new skills.
          </p>
        </div>
        <Button size="sm" disabled>
          <Plus className="mr-2 size-4" /> Add server
        </Button>
      </div>

      <Card className="border-dashed">
        <CardHeader className="flex flex-row items-center gap-3 space-y-0">
          <div className="bg-muted flex size-10 items-center justify-center rounded-lg">
            <Plug className="size-5" />
          </div>
          <div>
            <CardTitle className="text-base">No servers connected yet</CardTitle>
            <CardDescription>
              MCP connections arrive in Phase 5 with per-server guardrails and approval flows.
            </CardDescription>
          </div>
        </CardHeader>
        <CardContent />
      </Card>
    </div>
  );
}
