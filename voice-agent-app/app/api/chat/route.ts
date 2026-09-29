import { createOpenAI } from "@ai-sdk/openai";
import { frontendTools } from "@assistant-ui/ai-sdk";
import {
  type JSONSchema7,
  streamText,
  convertToModelMessages,
  type UIMessage,
} from "ai";

export const maxDuration = 60;

// Luna agent (FastAPI + LangGraph) speaks OpenAI-compatible SSE. The AI SDK
// OpenAI provider can target it directly; we inject the caller's InsForge JWT
// per-request so the agent can authenticate + persist under the right user.
//
// Env:
//   LUNA_AGENT_URL         e.g. http://127.0.0.1:8765 (local) or https://luna-agent...azurecontainerapps.io
//   LUNA_AGENT_MODEL       default model id the agent should run (informational; agent owns model config)
// Fallback (Phase 1 behavior): direct-to-Azure if no agent URL is configured.
const AGENT_URL = process.env.LUNA_AGENT_URL?.replace(/\/$/, "");
const AGENT_MODEL = process.env.LUNA_AGENT_MODEL ?? "gpt-5.6-luna";

const foundryFallback = createOpenAI({
  baseURL:
    process.env.AZURE_OPENAI_BASE_URL ??
    "https://datumm-agent-resource.services.ai.azure.com/openai/v1",
  apiKey: process.env.AZURE_OPENAI_API_KEY ?? "",
});
const FALLBACK_MODEL = process.env.AZURE_OPENAI_MODEL ?? "gpt-5.6-luna";

export async function POST(req: Request) {
  const {
    messages,
    system,
    tools,
  }: {
    messages: UIMessage[];
    system?: string;
    tools?: Record<string, { description?: string; parameters: JSONSchema7 }>;
  } = await req.json();

  if (!AGENT_URL) {
    // Phase 1 fallback: direct to Azure, no agent/memory.
    const result = streamText({
      model: foundryFallback(FALLBACK_MODEL),
      messages: await convertToModelMessages(messages),
      system,
      tools: { ...frontendTools(tools ?? {}) },
    });
    return result.toUIMessageStreamResponse({
      onError: (e) => (e instanceof Error ? e.message : String(e)),
    });
  }

  // Agent path: forward the user's InsForge access token.
  const auth = req.headers.get("authorization") ?? "";
  const token = auth.toLowerCase().startsWith("bearer ")
    ? auth.slice(7).trim()
    : "";
  if (!token) {
    return new Response(JSON.stringify({ error: "missing authorization" }), {
      status: 401,
      headers: { "content-type": "application/json" },
    });
  }

  // Custom fetch injects the Bearer token into every agent call. The AI SDK
  // uses this fetch for the /chat/completions request.
  const agentFetch: typeof fetch = async (input, init) => {
    const headers = new Headers(init?.headers ?? {});
    headers.set("authorization", `Bearer ${token}`);
    return fetch(input, { ...init, headers });
  };

  const agent = createOpenAI({
    baseURL: `${AGENT_URL}/v1`,
    // The apiKey is required by the SDK but unused by the agent (auth is via
    // the Authorization header we inject in agentFetch). Provide a placeholder.
    apiKey: "luna-agent",
    fetch: agentFetch,
  });

  const result = streamText({
    model: agent.chat(AGENT_MODEL),
    messages: await convertToModelMessages(messages),
    system,
    tools: { ...frontendTools(tools ?? {}) },
  });

  return result.toUIMessageStreamResponse({
    onError: (e) => (e instanceof Error ? e.message : String(e)),
  });
}
