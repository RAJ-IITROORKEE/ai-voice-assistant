"use client";

import { Globe, Wrench } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Switch } from "@/components/ui/switch";

type Tool = {
  id: string;
  name: string;
  description: string;
  source: "built-in" | "mcp";
  enabled: boolean;
};

const TOOLS: Tool[] = [
  {
    id: "web_search",
    name: "Web Search",
    description: "Search the web for current information and answer with sources.",
    source: "built-in",
    enabled: false,
  },
];

export function ToolsPanel() {
  return (
    <div className="mx-auto flex h-full max-w-3xl flex-col gap-4 overflow-y-auto p-6">
      <div>
        <h2 className="text-lg font-semibold">Active tools</h2>
        <p className="text-muted-foreground text-sm">
          Capabilities the agent can use. MCP-provided tools appear here once servers are connected.
        </p>
      </div>

      {TOOLS.map((t) => (
        <Card key={t.id}>
          <CardHeader className="flex flex-row items-center justify-between space-y-0">
            <div className="flex items-center gap-3">
              <div className="bg-muted flex size-10 items-center justify-center rounded-lg">
                {t.id === "web_search" ? (
                  <Globe className="size-5" />
                ) : (
                  <Wrench className="size-5" />
                )}
              </div>
              <div>
                <CardTitle className="text-base">{t.name}</CardTitle>
                <CardDescription>{t.description}</CardDescription>
              </div>
            </div>
            <div className="flex items-center gap-3">
              <Badge variant="outline">{t.source}</Badge>
              <Switch defaultChecked={t.enabled} disabled />
            </div>
          </CardHeader>
          <CardContent />
        </Card>
      ))}

      <p className="text-muted-foreground text-xs">
        Tool execution is enabled in Phase 5 once the agent service and guardrails are live.
      </p>
    </div>
  );
}
