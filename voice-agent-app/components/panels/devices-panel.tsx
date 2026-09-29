"use client";

import { Cpu, CircleDot, RefreshCw } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";

type Device = {
  id: string;
  name: string;
  deviceId: string;
  firmware: string;
  lastSeen: string;
  online: boolean;
};

// Placeholder data until InsForge `devices` table is wired (Phase 1 completion).
const PLACEHOLDER: Device[] = [
  {
    id: "1",
    name: "Atom VoiceS3R",
    deviceId: "b4:3a:45:bd:1d:9c",
    firmware: "13a0700-dirty",
    lastSeen: "just now",
    online: true,
  },
];

export function DevicesPanel() {
  return (
    <div className="mx-auto flex h-full max-w-3xl flex-col gap-4 overflow-y-auto p-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-semibold">Connected devices</h2>
          <p className="text-muted-foreground text-sm">
            Voice hardware linked to your account.
          </p>
        </div>
        <Button variant="outline" size="sm">
          <RefreshCw className="mr-2 size-4" /> Refresh
        </Button>
      </div>

      {PLACEHOLDER.map((d) => (
        <Card key={d.id}>
          <CardHeader className="flex flex-row items-center justify-between space-y-0">
            <div className="flex items-center gap-3">
              <div className="bg-muted flex size-10 items-center justify-center rounded-lg">
                <Cpu className="size-5" />
              </div>
              <div>
                <CardTitle className="text-base">{d.name}</CardTitle>
                <CardDescription className="font-mono text-xs">
                  {d.deviceId}
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
            <span className="text-foreground font-mono">{d.firmware}</span>
            <span>Last seen</span>
            <span className="text-foreground">{d.lastSeen}</span>
          </CardContent>
        </Card>
      ))}
    </div>
  );
}
