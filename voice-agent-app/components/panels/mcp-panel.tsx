"use client";

import * as React from "react";
import { Loader2, Plug, Plus, RefreshCw } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { insforge } from "@/lib/insforge";

type McpServerRow = {
  id: string;
  name: string;
  url: string;
  transport: string;
  enabled: boolean;
  read_only: boolean;
  status: string;
  created_at: string;
};

const POLL_MS = 5000;

function truncate(s: string, max: number): string {
  return s.length > max ? `${s.slice(0, max)}…` : s;
}

export function McpPanel() {
  const [servers, setServers] = React.useState<McpServerRow[]>([]);
  const [loading, setLoading] = React.useState(true);
  const [refreshing, setRefreshing] = React.useState(false);
  const [error, setError] = React.useState<string | null>(null);

  const load = React.useCallback(async (showSpinner = false) => {
    if (showSpinner) setRefreshing(true);
    try {
      const { data, error } = await insforge.database
        .from("mcp_servers")
        .select("id,name,url,transport,enabled,read_only,status,created_at")
        .order("created_at", { ascending: false });
      if (error) throw error;
      setServers((data as McpServerRow[] | null) ?? []);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load MCP servers");
    } finally {
      setLoading(false);
      if (showSpinner) setRefreshing(false);
    }
  }, []);

  React.useEffect(() => {
    load();
    const t = setInterval(() => load(), POLL_MS);
    return () => clearInterval(t);
  }, [load]);

  return (
    <div className="mx-auto flex h-full max-w-3xl flex-col gap-4 overflow-y-auto p-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold">MCP servers</h2>
          <p className="text-muted-foreground text-sm">
            Connect Model Context Protocol servers (Notion, Gmail, …) to give the agent new skills.
          </p>
        </div>
        <div className="flex items-center gap-2">
          <Button
            variant="outline"
            size="sm"
            onClick={() => load(true)}
            disabled={refreshing}
          >
            {refreshing ? (
              <Loader2 className="mr-2 size-4 animate-spin" />
            ) : (
              <RefreshCw className="mr-2 size-4" />
            )}
            Refresh
          </Button>
          <Button size="sm" disabled title="Coming in Phase 6">
            <Plus className="mr-2 size-4" /> Add server
          </Button>
        </div>
      </div>

      <p className="text-muted-foreground text-xs">
        Adding servers is coming in Phase 6.
      </p>

      {error && (
        <p className="text-destructive text-sm">Failed to load MCP servers: {error}</p>
      )}

      {loading ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading MCP servers…
        </div>
      ) : servers.length === 0 ? (
        <Card>
          <CardContent className="text-muted-foreground flex flex-col items-center gap-2 py-10 text-sm">
            <Plug className="size-6" />
            No MCP servers connected yet.
          </CardContent>
        </Card>
      ) : (
        servers.map((s) => (
          <Card key={s.id}>
            <CardHeader className="flex flex-row items-center justify-between space-y-0">
              <div className="flex min-w-0 items-center gap-3">
                <div className="bg-muted flex size-10 shrink-0 items-center justify-center rounded-lg">
                  <Plug className="size-5" />
                </div>
                <div className="min-w-0">
                  <CardTitle className="truncate text-base">{s.name}</CardTitle>
                  <CardDescription className="truncate font-mono text-xs">
                    {truncate(s.url, 80)}
                  </CardDescription>
                </div>
              </div>
              <div className="flex shrink-0 flex-wrap items-center justify-end gap-2">
                <Badge variant="outline">{s.transport}</Badge>
                <Badge variant={s.status === "connected" ? "default" : "secondary"}>
                  {s.status}
                </Badge>
                <Badge variant={s.enabled ? "default" : "outline"}>
                  {s.enabled ? "enabled" : "disabled"}
                </Badge>
                {s.read_only && <Badge variant="secondary">read-only</Badge>}
              </div>
            </CardHeader>
            <CardContent />
          </Card>
        ))
      )}
    </div>
  );
}
