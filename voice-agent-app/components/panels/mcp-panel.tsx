"use client";

import * as React from "react";
import { Loader2, Plug, Plus, RefreshCw, Trash2 } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
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

  // Add-server dialog state
  const [addOpen, setAddOpen] = React.useState(false);
  const [name, setName] = React.useState("");
  const [url, setUrl] = React.useState("");
  const [transport, setTransport] = React.useState("streamable_http");
  const [authToken, setAuthToken] = React.useState("");
  const [readOnly, setReadOnly] = React.useState(true);
  const [allowedTools, setAllowedTools] = React.useState("");
  const [enabled, setEnabled] = React.useState(true);
  const [saving, setSaving] = React.useState(false);
  const [formError, setFormError] = React.useState<string | null>(null);

  // Delete state
  const [deletingId, setDeletingId] = React.useState<string | null>(null);

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

  const resetForm = () => {
    setName("");
    setUrl("");
    setTransport("streamable_http");
    setAuthToken("");
    setReadOnly(true);
    setAllowedTools("");
    setEnabled(true);
    setFormError(null);
  };

  const createServer = async () => {
    if (!name.trim() || !url.trim()) {
      setFormError("Name and Server URL are required.");
      return;
    }
    setSaving(true);
    setFormError(null);
    try {
      const { data: auth } = await insforge.auth.getCurrentUser();
      const userId = (auth?.user as { id?: string } | null)?.id;
      if (!userId) throw new Error("Not signed in.");

      const { error } = await insforge.database.from("mcp_servers").insert([
        {
          name: name.trim(),
          url: url.trim(),
          transport,
          auth_token: authToken.trim() || null,
          read_only: readOnly,
          allowed_tools: allowedTools.trim()
            ? allowedTools
                .split(",")
                .map((t) => t.trim())
                .filter(Boolean)
            : [],
          enabled,
          status: "disconnected",
          user_id: userId,
        },
      ]);
      if (error) throw error;
      setAddOpen(false);
      resetForm();
      await load();
    } catch (e) {
      setFormError(e instanceof Error ? e.message : "Failed to add server");
    } finally {
      setSaving(false);
    }
  };

  const removeServer = async (id: string) => {
    if (!window.confirm("Delete this MCP server?")) return;
    setDeletingId(id);
    setError(null);
    try {
      const { error } = await insforge.database
        .from("mcp_servers")
        .delete()
        .eq("id", id);
      if (error) throw error;
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to delete server");
    } finally {
      setDeletingId(null);
    }
  };

  return (
    <div className="mx-auto flex min-h-0 w-full max-w-3xl flex-1 flex-col gap-4 overflow-y-auto p-6">
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
          <Button size="sm" onClick={() => setAddOpen(true)}>
            <Plus className="mr-2 size-4" /> Add server
          </Button>
        </div>
      </div>

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
                <Button
                  variant="ghost"
                  size="icon-sm"
                  title="Delete"
                  disabled={deletingId === s.id}
                  onClick={() => removeServer(s.id)}
                >
                  {deletingId === s.id ? (
                    <Loader2 className="size-4 animate-spin" />
                  ) : (
                    <Trash2 className="size-4" />
                  )}
                </Button>
              </div>
            </CardHeader>
            <CardContent />
          </Card>
        ))
      )}

      {/* Add server dialog */}
      <Dialog
        open={addOpen}
        onOpenChange={(v) => {
          setAddOpen(v);
          if (!v) resetForm();
        }}
      >
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Add MCP server</DialogTitle>
            <DialogDescription>
              Connect a streamable-http or SSE MCP endpoint.
            </DialogDescription>
          </DialogHeader>
          <div className="grid gap-4">
            <div className="grid gap-2">
              <Label htmlFor="mcp-name">Name</Label>
              <Input
                id="mcp-name"
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="Notion"
              />
            </div>
            <div className="grid gap-2">
              <Label htmlFor="mcp-url">Server URL</Label>
              <Input
                id="mcp-url"
                value={url}
                onChange={(e) => setUrl(e.target.value)}
                placeholder="https://mcp.example.com/mcp"
                className="font-mono"
              />
            </div>
            <div className="grid gap-2">
              <Label>Transport</Label>
              <Select value={transport} onValueChange={(v) => v && setTransport(v)}>
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="streamable_http">streamable_http</SelectItem>
                  <SelectItem value="sse">sse</SelectItem>
                </SelectContent>
              </Select>
            </div>
            <div className="grid gap-2">
              <Label htmlFor="mcp-token">Auth token (optional)</Label>
              <Input
                id="mcp-token"
                type="password"
                value={authToken}
                onChange={(e) => setAuthToken(e.target.value)}
                placeholder="Bearer token"
              />
            </div>
            <div className="grid gap-2">
              <Label htmlFor="mcp-tools">Allowed tools (optional)</Label>
              <Input
                id="mcp-tools"
                value={allowedTools}
                onChange={(e) => setAllowedTools(e.target.value)}
                placeholder="search, create_page (blank = all)"
              />
              <p className="text-muted-foreground text-xs">
                Comma-separated tool names; leave blank to allow all tools.
              </p>
            </div>
            <div className="flex items-center justify-between gap-4">
              <div>
                <Label htmlFor="mcp-readonly">Read-only</Label>
                <p className="text-muted-foreground text-xs">
                  Only expose non-mutating tools.
                </p>
              </div>
              <Switch
                id="mcp-readonly"
                checked={readOnly}
                onCheckedChange={(v) => setReadOnly(v === true)}
              />
            </div>
            <div className="flex items-center justify-between gap-4">
              <div>
                <Label htmlFor="mcp-enabled">Enabled</Label>
                <p className="text-muted-foreground text-xs">
                  Disabled servers are ignored by the agent.
                </p>
              </div>
              <Switch
                id="mcp-enabled"
                checked={enabled}
                onCheckedChange={(v) => setEnabled(v === true)}
              />
            </div>
            {formError && <p className="text-destructive text-sm">{formError}</p>}
          </div>
          <DialogFooter>
            <Button
              variant="outline"
              onClick={() => {
                setAddOpen(false);
                resetForm();
              }}
              disabled={saving}
            >
              Cancel
            </Button>
            <Button onClick={createServer} disabled={saving}>
              {saving && <Loader2 className="mr-2 size-4 animate-spin" />}
              {saving ? "Adding…" : "Add server"}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
