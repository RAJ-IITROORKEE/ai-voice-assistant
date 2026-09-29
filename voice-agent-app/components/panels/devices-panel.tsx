"use client";

import * as React from "react";
import { Cpu, CircleDot, RefreshCw, Loader2 } from "lucide-react";
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

type DeviceRow = {
  id: string;
  name: string;
  device_id: string;
  firmware: string | null;
  last_seen: string | null;
  online: boolean;
};

const POLL_MS = 3000;

function relativeTime(iso: string | null): string {
  if (!iso) return "never";
  const then = new Date(iso).getTime();
  if (Number.isNaN(then)) return "never";
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

export function DevicesPanel() {
  const [devices, setDevices] = React.useState<DeviceRow[]>([]);
  const [loading, setLoading] = React.useState(true);
  const [refreshing, setRefreshing] = React.useState(false);
  const [error, setError] = React.useState<string | null>(null);

  const load = React.useCallback(async (showSpinner = false) => {
    if (showSpinner) setRefreshing(true);
    try {
      const { data, error } = await insforge.database
        .from("devices")
        .select("id,name,device_id,firmware,last_seen,online")
        .order("last_seen", { ascending: false });
      if (error) throw error;
      setDevices((data as DeviceRow[] | null) ?? []);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load devices");
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
          <h2 className="text-lg font-semibold">Connected devices</h2>
          <p className="text-muted-foreground text-sm">
            Voice hardware linked to your account.
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
        <p className="text-destructive text-sm">Failed to load devices: {error}</p>
      )}

      {loading ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading devices…
        </div>
      ) : devices.length === 0 ? (
        <Card>
          <CardContent className="text-muted-foreground flex flex-col items-center gap-2 py-10 text-sm">
            <Cpu className="size-6" />
            No devices yet. Power on your Luna device and it will appear here.
          </CardContent>
        </Card>
      ) : (
        devices.map((d) => (
          <Card key={d.id}>
            <CardHeader className="flex flex-row items-center justify-between space-y-0">
              <div className="flex items-center gap-3">
                <div className="bg-muted flex size-10 items-center justify-center rounded-lg">
                  <Cpu className="size-5" />
                </div>
                <div>
                  <CardTitle className="text-base">{d.name}</CardTitle>
                  <CardDescription className="font-mono text-xs">
                    {d.device_id}
                  </CardDescription>
                </div>
              </div>
              <Badge variant={d.online ? "default" : "secondary"}>
                <CircleDot className="mr-1 size-3" />
                {d.online ? "Online" : "Offline"}
              </Badge>
            </CardHeader>
            <CardContent className="text-muted-foreground grid grid-cols-2 gap-2 text-sm">
              <span>Firmware</span>
              <span className="text-foreground font-mono">
                {d.firmware ?? "—"}
              </span>
              <span>Last seen</span>
              <span className="text-foreground">{relativeTime(d.last_seen)}</span>
            </CardContent>
          </Card>
        ))
      )}
    </div>
  );
}
