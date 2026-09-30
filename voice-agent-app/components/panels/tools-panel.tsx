"use client";

import * as React from "react";
import { Loader2, RefreshCw, Wrench } from "lucide-react";
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

type BuiltInTool = {
  id: string;
  name: string;
  description: string;
};

// Hardwired in the Python agent — always on, no toggle.
const BUILT_IN_TOOLS: BuiltInTool[] = [
  {
    id: "web_search",
    name: "web_search",
    description: "Search the web (DuckDuckGo/Wikipedia) for facts and definitions.",
  },
  {
    id: "calculator",
    name: "calculator",
    description: "Evaluate arithmetic safely (+ - * / % // ** and parentheses).",
  },
  {
    id: "get_current_time",
    name: "get_current_time",
    description: "Get the current UTC time (ISO 8601).",
  },
  {
    id: "set_reminder",
    name: "set_reminder",
    description: "Set a reminder for a task at a given time.",
  },
  {
    id: "list_reminders_tool",
    name: "list_reminders",
    description: "List your open reminders.",
  },
  {
    id: "save_note_tool",
    name: "save_note",
    description: "Save a structured note or task with title and body.",
  },
  {
    id: "list_notes_tool",
    name: "list_notes",
    description: "List your saved notes and tasks.",
  },
];

type ToolCallRow = {
  id: string;
  server: string | null;
  tool: string;
  args: unknown;
  result_summary: string | null;
  status: string;
  created_at: string;
};

const POLL_MS = 3000;

function relativeTime(iso: string): string {
  const then = new Date(iso).getTime();
  if (Number.isNaN(then)) return "";
  const diff = Date.now() - then;
  if (diff < 5_000) return "just now";
  const sec = Math.floor(diff / 1000);
  if (sec < 60) return `${sec}s ago`;
  const min = Math.floor(sec / 60);
  if (min < 60) return `${min}m ago`;
  const hr = Math.floor(min / 60);
  if (hr < 24) return `${hr}h ago`;
  const day = Math.floor(hr / 24);
  return `${day}d ago`;
}

function truncate(s: string, max: number): string {
  return s.length > max ? `${s.slice(0, max)}…` : s;
}

function compactArgs(args: unknown): string {
  if (args === null || args === undefined) return "";
  try {
    return truncate(JSON.stringify(args), 80);
  } catch {
    return "";
  }
}

export function ToolsPanel() {
  const [calls, setCalls] = React.useState<ToolCallRow[]>([]);
  const [loading, setLoading] = React.useState(true);
  const [refreshing, setRefreshing] = React.useState(false);
  const [error, setError] = React.useState<string | null>(null);

  const load = React.useCallback(async (showSpinner = false) => {
    if (showSpinner) setRefreshing(true);
    try {
      const { data, error } = await insforge.database
        .from("tool_calls")
        .select("id,server,tool,args,result_summary,status,created_at")
        .order("created_at", { ascending: false })
        .limit(20);
      if (error) throw error;
      setCalls((data as ToolCallRow[] | null) ?? []);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load tool calls");
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
    <div className="mx-auto flex min-h-0 w-full max-w-3xl flex-1 flex-col gap-4 overflow-y-auto p-6">
      <div>
        <h2 className="text-lg font-semibold">Active tools</h2>
        <p className="text-muted-foreground text-sm">
          Capabilities the agent can use. MCP-provided tools appear here once servers are connected.
        </p>
      </div>

      <div className="grid gap-2 sm:grid-cols-2">
        {BUILT_IN_TOOLS.map((t) => (
          <div
            key={t.id}
            className="flex items-start gap-3 rounded-lg border px-3 py-2.5"
          >
            <div className="bg-muted flex size-8 shrink-0 items-center justify-center rounded-md">
              <Wrench className="text-muted-foreground size-4" />
            </div>
            <div className="min-w-0 flex-1">
              <div className="flex items-center gap-2">
                <span className="truncate text-sm font-medium">{t.name}</span>
                <Badge variant="default" className="shrink-0 px-1.5 py-0 text-[10px]">
                  active
                </Badge>
              </div>
              <p className="text-muted-foreground mt-0.5 text-xs leading-snug">
                {t.description}
              </p>
            </div>
          </div>
        ))}
      </div>

      <div className="mt-4 flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold">Recent tool calls</h2>
          <p className="text-muted-foreground text-sm">
            The last 20 tool executions across your account.
          </p>
        </div>
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
      </div>

      {error && (
        <p className="text-destructive text-sm">Failed to load tool calls: {error}</p>
      )}

      {loading ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading tool calls…
        </div>
      ) : calls.length === 0 ? (
        <Card>
          <CardContent className="text-muted-foreground flex flex-col items-center gap-2 py-10 text-sm">
            <Wrench className="size-6" />
            No tool calls yet — ask Luna to calculate something.
          </CardContent>
        </Card>
      ) : (
        <div className="flex flex-col gap-2">
          {calls.map((c) => (
            <div
              key={c.id}
              className="flex items-center gap-3 rounded-lg border px-3 py-2.5"
            >
              <div className="bg-muted flex size-8 shrink-0 items-center justify-center rounded-md">
                <Wrench className="text-muted-foreground size-4" />
              </div>
              <div className="min-w-0 flex-1">
                <div className="flex items-center gap-2">
                  <span className="truncate text-sm font-medium">{c.tool}</span>
                  {c.server && c.server !== "built-in" && (
                    <Badge variant="outline" className="shrink-0 px-1.5 py-0 text-[10px]">
                      {c.server}
                    </Badge>
                  )}
                  <Badge
                    variant={c.status === "success" ? "default" : "destructive"}
                    className="shrink-0 px-1.5 py-0 text-[10px]"
                  >
                    {c.status}
                  </Badge>
                </div>
                {c.result_summary && (
                  <p className="mt-0.5 truncate text-xs">
                    {truncate(c.result_summary, 80)}
                  </p>
                )}
                {compactArgs(c.args) && (
                  <p className="text-muted-foreground mt-0.5 truncate font-mono text-[11px]">
                    {compactArgs(c.args)}
                  </p>
                )}
              </div>
              <span className="text-muted-foreground shrink-0 text-xs">
                {relativeTime(c.created_at)}
              </span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
