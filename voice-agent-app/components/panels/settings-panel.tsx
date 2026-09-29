"use client";

import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Textarea } from "@/components/ui/textarea";
import { ModeToggle } from "@/components/mode-toggle";

export function SettingsPanel() {
  return (
    <div className="mx-auto flex h-full max-w-3xl flex-col gap-4 overflow-y-auto p-6">
      <div>
        <h2 className="text-lg font-semibold">Assistant settings</h2>
        <p className="text-muted-foreground text-sm">
          Control how Luna thinks and sounds. Applies to web chat and linked devices.
        </p>
      </div>

      <Card>
        <CardHeader>
          <CardTitle className="text-base">Appearance</CardTitle>
          <CardDescription>Dark mode is the default; toggle anytime.</CardDescription>
        </CardHeader>
        <CardContent>
          <ModeToggle />
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="text-base">Voice pipeline</CardTitle>
          <CardDescription>
            Classic STT→LLM→TTS is the stable default. Realtime speech-to-speech arrives in Phase 4.
          </CardDescription>
        </CardHeader>
        <CardContent className="grid gap-4">
          <div className="grid gap-2">
            <Label>Pipeline</Label>
            <Select defaultValue="classic" disabled>
              <SelectTrigger><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="classic">Classic (Azure STT → LLM → TTS)</SelectItem>
                <SelectItem value="azure-realtime">Azure Realtime (Phase 4)</SelectItem>
                <SelectItem value="gemini-live">Gemini Live (Phase 4)</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div className="grid gap-2">
            <Label>Model</Label>
            <Select defaultValue="deepseek-flash" disabled>
              <SelectTrigger><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="deepseek-flash">DeepSeek-V4-Flash (fast, cheap)</SelectItem>
                <SelectItem value="gpt-5.6-luna">gpt-5.6-luna (balanced)</SelectItem>
                <SelectItem value="gpt-6-astra">gpt-6-astra (quality)</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div className="grid gap-2">
            <Label>Voice</Label>
            <Select defaultValue="neerja" disabled>
              <SelectTrigger><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="neerja">en-IN Neerja (current)</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div className="grid gap-2">
            <Label>Language</Label>
            <Select defaultValue="en-IN" disabled>
              <SelectTrigger><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="en-IN">English (India)</SelectItem>
                <SelectItem value="hi-IN">Hindi</SelectItem>
              </SelectContent>
            </Select>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="text-base">Persona</CardTitle>
          <CardDescription>System instructions for the assistant.</CardDescription>
        </CardHeader>
        <CardContent>
          <Textarea
            placeholder="You are Luna, a concise, friendly voice assistant…"
            defaultValue="You are Luna, a concise, friendly voice assistant. Answer briefly and clearly; responses are spoken aloud."
            disabled
          />
        </CardContent>
      </Card>

      <p className="text-muted-foreground text-xs">
        Editing is enabled once the settings store is connected (end of Phase 1).
      </p>
    </div>
  );
}
