"use client";

import * as React from "react";
import {
  MessagesSquare,
  RefreshCw,
  Loader2,
  Pin,
  PinOff,
  Archive,
  ArchiveRestore,
  Trash2,
  Check,
} from "lucide-react";
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
import { insforge } from "@/lib/insforge";

type Conversation = {
  id: string;
  title: string;
  source: string;
  pinned: boolean;
  archived: boolean;
  updated_at: string;
};

type Message = {
  id: string;
  role: string;
  content: string;
  created_at: string;
};

const POLL_MS = 2000;

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

export function ConversationsPanel() {
  const [items, setItems] = React.useState<Conversation[]>([]);
  const [loading, setLoading] = React.useState(true);
  const [error, setError] = React.useState<string | null>(null);
  const [showArchived, setShowArchived] = React.useState(false);
  const [openId, setOpenId] = React.useState<string | null>(null);
  const [messages, setMessages] = React.useState<Message[]>([]);
  const [messagesLoading, setMessagesLoading] = React.useState(false);
  const [confirmDeleteAll, setConfirmDeleteAll] = React.useState(false);
  const [busy, setBusy] = React.useState(false);

  const load = React.useCallback(async () => {
    try {
      const { data, error } = await insforge.database
        .from("conversations")
        .select("id,title,source,pinned,archived,updated_at")
        .eq("deleted", false)
        .order("pinned", { ascending: false })
        .order("updated_at", { ascending: false });
      if (error) throw error;
      setItems((data as Conversation[] | null) ?? []);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load conversations");
    } finally {
      setLoading(false);
    }
  }, []);

  React.useEffect(() => {
    load();
    const t = setInterval(() => load(), POLL_MS);
    return () => clearInterval(t);
  }, [load]);

  const update = async (
    id: string,
    patch: { pinned?: boolean; archived?: boolean; deleted?: boolean }
  ) => {
    setBusy(true);
    try {
      const { error } = await insforge.database
        .from("conversations")
        .update(patch)
        .eq("id", id);
      if (error) throw error;
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Update failed");
    } finally {
      setBusy(false);
    }
  };

  const remove = (id: string) => update(id, { deleted: true });

  const deleteAll = async () => {
    setBusy(true);
    try {
      const { error } = await insforge.database
        .from("conversations")
        .update({ deleted: true })
        .eq("deleted", false);
      if (error) throw error;
      setConfirmDeleteAll(false);
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Delete all failed");
    } finally {
      setBusy(false);
    }
  };

  const openConversation = async (id: string) => {
    setOpenId(id);
    setMessagesLoading(true);
    try {
      const { data, error } = await insforge.database
        .from("messages")
        .select("id,role,content,created_at")
        .eq("conversation_id", id)
        .order("created_at", { ascending: true });
      if (error) throw error;
      setMessages((data as Message[] | null) ?? []);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load messages");
      setMessages([]);
    } finally {
      setMessagesLoading(false);
    }
  };

  const visible = items.filter((c) => (showArchived ? true : !c.archived));
  const open = items.find((c) => c.id === openId) ?? null;

  return (
    <div className="mx-auto flex h-full max-w-3xl flex-col gap-4 overflow-y-auto p-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold">Conversations</h2>
          <p className="text-muted-foreground text-sm">
            Web chats and device voice turns.
          </p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="outline" size="sm" onClick={() => load()}>
            <RefreshCw className="mr-2 size-4" /> Refresh
          </Button>
          <Button
            variant="destructive"
            size="sm"
            onClick={() => setConfirmDeleteAll(true)}
            disabled={items.length === 0}
          >
            <Trash2 className="mr-2 size-4" /> Delete all
          </Button>
        </div>
      </div>

      <div className="flex items-center gap-2">
        <Button
          variant={showArchived ? "default" : "outline"}
          size="sm"
          onClick={() => setShowArchived((v) => !v)}
        >
          <Archive className="mr-2 size-4" />
          {showArchived ? "Showing archived" : "Show archived"}
        </Button>
      </div>

      {error && <p className="text-destructive text-sm">{error}</p>}

      {loading ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading conversations…
        </div>
      ) : visible.length === 0 ? (
        <Card>
          <CardContent className="text-muted-foreground flex flex-col items-center gap-2 py-10 text-sm">
            <MessagesSquare className="size-6" />
            No conversations yet.
          </CardContent>
        </Card>
      ) : (
        visible.map((c) => (
          <Card key={c.id}>
            <CardHeader className="flex flex-row items-center justify-between space-y-0">
              <button
                className="flex min-w-0 flex-1 items-center gap-3 text-left"
                onClick={() => openConversation(c.id)}
              >
                <div className="bg-muted flex size-10 shrink-0 items-center justify-center rounded-lg">
                  <MessagesSquare className="size-5" />
                </div>
                <div className="min-w-0">
                  <CardTitle className="truncate text-base">
                    {c.title || "Untitled"}
                  </CardTitle>
                  <CardDescription className="text-xs">
                    {relativeTime(c.updated_at)}
                  </CardDescription>
                </div>
              </button>
              <div className="flex shrink-0 items-center gap-1">
                <Badge variant={c.source === "device" ? "default" : "secondary"}>
                  {c.source}
                </Badge>
                {c.archived && <Badge variant="outline">archived</Badge>}
                <Button
                  variant="ghost"
                  size="icon-sm"
                  title={c.pinned ? "Unpin" : "Pin"}
                  disabled={busy}
                  onClick={() => update(c.id, { pinned: !c.pinned })}
                >
                  {c.pinned ? <PinOff className="size-4" /> : <Pin className="size-4" />}
                </Button>
                <Button
                  variant="ghost"
                  size="icon-sm"
                  title={c.archived ? "Unarchive" : "Archive"}
                  disabled={busy}
                  onClick={() => update(c.id, { archived: !c.archived })}
                >
                  {c.archived ? (
                    <ArchiveRestore className="size-4" />
                  ) : (
                    <Archive className="size-4" />
                  )}
                </Button>
                <Button
                  variant="ghost"
                  size="icon-sm"
                  title="Delete"
                  disabled={busy}
                  onClick={() => remove(c.id)}
                >
                  <Trash2 className="size-4" />
                </Button>
              </div>
            </CardHeader>
          </Card>
        ))
      )}

      {/* Messages viewer */}
      <Dialog open={openId !== null} onOpenChange={(v) => !v && setOpenId(null)}>
        <DialogContent className="max-h-[80vh] overflow-y-auto sm:max-w-lg">
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              {open?.title || "Conversation"}
              {open && (
                <Badge variant={open.source === "device" ? "default" : "secondary"}>
                  {open.source}
                </Badge>
              )}
            </DialogTitle>
            <DialogDescription>
              {open ? relativeTime(open.updated_at) : ""}
            </DialogDescription>
          </DialogHeader>
          {messagesLoading ? (
            <div className="text-muted-foreground flex items-center gap-2 py-6 text-sm">
              <Loader2 className="size-4 animate-spin" /> Loading messages…
            </div>
          ) : messages.length === 0 ? (
            <p className="text-muted-foreground py-6 text-sm">No messages.</p>
          ) : (
            <div className="flex flex-col gap-3 py-2">
              {messages.map((m) => (
                <div
                  key={m.id}
                  className={
                    m.role === "user"
                      ? "bg-muted self-end rounded-lg px-3 py-2 text-sm"
                      : "self-start rounded-lg border px-3 py-2 text-sm"
                  }
                >
                  <div className="text-muted-foreground mb-1 text-xs capitalize">
                    {m.role}
                  </div>
                  <div className="whitespace-pre-wrap">{m.content}</div>
                </div>
              ))}
            </div>
          )}
        </DialogContent>
      </Dialog>

      {/* Delete all confirmation */}
      <Dialog open={confirmDeleteAll} onOpenChange={setConfirmDeleteAll}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Delete all conversations?</DialogTitle>
            <DialogDescription>
              This hides all {items.length} conversation
              {items.length === 1 ? "" : "s"}. You can't undo this from the app.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button
              variant="outline"
              onClick={() => setConfirmDeleteAll(false)}
              disabled={busy}
            >
              Cancel
            </Button>
            <Button variant="destructive" onClick={deleteAll} disabled={busy}>
              {busy ? (
                <Loader2 className="mr-2 size-4 animate-spin" />
              ) : (
                <Check className="mr-2 size-4" />
              )}
              Delete all
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
