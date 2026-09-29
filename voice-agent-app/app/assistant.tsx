"use client";

import { AssistantRuntimeProvider } from "@assistant-ui/react";
import { useChatRuntime, AssistantChatTransport } from "@assistant-ui/ai-sdk";
import { lastAssistantMessageIsCompleteWithToolCalls } from "ai";
import { Thread } from "@/components/assistant-ui/elements/thread.aui";
import { ThreadList } from "@/components/assistant-ui/elements/thread-list.aui";
import { AppShell } from "@/components/app-shell";
import { insforge } from "@/lib/insforge";

export const Assistant = () => {
  const runtime = useChatRuntime({
    sendAutomaticallyWhen: lastAssistantMessageIsCompleteWithToolCalls,
    transport: new AssistantChatTransport({
      api: "/api/chat",
      // Forward the signed-in user's InsForge access token so the relay/agent
      // can authenticate + persist under this user. getValidAccessToken()
      // refreshes if needed and returns null when signed out.
      headers: async (): Promise<Record<string, string>> => {
        try {
          const token = await insforge.getHttpClient().getValidAccessToken();
          return token ? { Authorization: `Bearer ${token}` } : {};
        } catch {
          return {};
        }
      },
    }),
  });

  return (
    <AssistantRuntimeProvider runtime={runtime}>
      <AppShell active="chat" sidebarExtra={<ThreadList />}>
        <Thread />
      </AppShell>
    </AssistantRuntimeProvider>
  );
};
