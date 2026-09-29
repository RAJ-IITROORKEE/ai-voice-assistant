import { createOpenAI } from "@ai-sdk/openai";
import { frontendTools } from "@assistant-ui/ai-sdk";
import {
  type JSONSchema7,
  streamText,
  convertToModelMessages,
  type UIMessage,
} from "ai";

export const maxDuration = 30;

// Azure AI Foundry OpenAI-compatible endpoint (datumm-agent-resource).
// The base URL is derived from the relay's AzureOpenAI.Endpoint by stripping the
// trailing "/chat/completions" so the AI SDK can append the right path.
const foundry = createOpenAI({
  baseURL:
    process.env.AZURE_OPENAI_BASE_URL ??
    "https://datumm-agent-resource.services.ai.azure.com/openai/v1",
  apiKey: process.env.AZURE_OPENAI_API_KEY ?? "",
});

const MODEL = process.env.AZURE_OPENAI_MODEL ?? "gpt-5.6-luna";

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

  const result = streamText({
    model: foundry(MODEL),
    messages: await convertToModelMessages(messages),
    system,
    tools: {
      ...frontendTools(tools ?? {}),
    },
  });

  return result.toUIMessageStreamResponse({
    onError: (error) =>
      error instanceof Error ? error.message : String(error),
  });
}
