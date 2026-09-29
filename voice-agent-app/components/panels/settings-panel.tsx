"use client";

import * as React from "react";
import { Check, Loader2 } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Input } from "@/components/ui/input";
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
import { insforge } from "@/lib/insforge";

const DEFAULTS = {
  pipeline: "classic",
  model: "gpt-5.6-luna",
  voice: "en-IN-NeerjaNeural",
  language: "en-IN",
  persona:
    "You are Luna, a concise, friendly voice assistant. Answer briefly and clearly; responses are spoken aloud.",
};

export function SettingsPanel() {
  const [userId, setUserId] = React.useState<string | null>(null);
  const [pipeline, setPipeline] = React.useState(DEFAULTS.pipeline);
  const [model, setModel] = React.useState(DEFAULTS.model);
  const [voice, setVoice] = React.useState(DEFAULTS.voice);
  const [language, setLanguage] = React.useState(DEFAULTS.language);
  const [persona, setPersona] = React.useState(DEFAULTS.persona);
  const [loading, setLoading] = React.useState(true);
  const [saving, setSaving] = React.useState(false);
  const [saved, setSaved] = React.useState(false);
  const [error, setError] = React.useState<string | null>(null);

  React.useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const { data: auth } = await insforge.auth.getCurrentUser();
        const uid = (auth?.user as { id?: string } | null)?.id ?? null;
        if (!uid) return;
        if (!cancelled) setUserId(uid);
        const { data, error } = await insforge.database
          .from("settings")
          .select("pipeline,model,voice,language,persona")
          .eq("user_id", uid)
          .maybeSingle();
        if (error) throw error;
        if (!cancelled && data) {
          const row = data as Partial<typeof DEFAULTS>;
          if (row.pipeline) setPipeline(row.pipeline);
          if (row.model) setModel(row.model);
          if (row.voice) setVoice(row.voice);
          if (row.language) setLanguage(row.language);
          if (row.persona) setPersona(row.persona);
        }
      } catch (e) {
        if (!cancelled)
          setError(e instanceof Error ? e.message : "Failed to load settings");
      } finally {
        if (!cancelled) setLoading(false);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

  const save = async () => {
    if (!userId) return;
    setSaving(true);
    setSaved(false);
    setError(null);
    try {
      const { error } = await insforge.database.from("settings").upsert(
        {
          user_id: userId,
          pipeline,
          model,
          voice,
          language,
          persona,
        },
        { onConflict: "user_id" }
      );
      if (error) throw error;
      setSaved(true);
      setTimeout(() => setSaved(false), 2500);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to save settings");
    } finally {
      setSaving(false);
    }
  };

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
            <Select value={pipeline} onValueChange={(v) => v && setPipeline(v)}>
              <SelectTrigger><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="classic">Classic (Azure STT → LLM → TTS)</SelectItem>
                <SelectItem value="azure-realtime" disabled>Azure Realtime (Phase 4)</SelectItem>
                <SelectItem value="gemini-live" disabled>Gemini Live (Phase 4)</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div className="grid gap-2">
            <Label>Model</Label>
            <Select value={model} onValueChange={(v) => v && setModel(v)}>
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
            <Input
              value={voice}
              onChange={(e) => setVoice(e.target.value)}
              placeholder="en-IN-NeerjaNeural"
              className="font-mono"
            />
            <p className="text-muted-foreground text-xs">
              Azure voice name, e.g. en-IN-NeerjaNeural.
            </p>
          </div>
          <div className="grid gap-2">
            <Label>Language</Label>
            <Select value={language} onValueChange={(v) => v && setLanguage(v)}>
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
            value={persona}
            onChange={(e) => setPersona(e.target.value)}
            placeholder="You are Luna, a concise, friendly voice assistant…"
            rows={4}
          />
        </CardContent>
      </Card>

      <div className="flex items-center gap-3">
        <Button onClick={save} disabled={loading || saving || !userId}>
          {saving ? (
            <Loader2 className="mr-2 size-4 animate-spin" />
          ) : saved ? (
            <Check className="mr-2 size-4" />
          ) : null}
          {saving ? "Saving…" : saved ? "Saved" : "Save settings"}
        </Button>
        {saved && (
          <span className="text-muted-foreground inline-flex items-center gap-1 text-sm">
            <Check className="size-4 text-green-500" /> Saved
          </span>
        )}
        {error && <span className="text-destructive text-sm">{error}</span>}
      </div>
    </div>
  );
}
