import { AppShell } from "@/components/app-shell";
import { McpPanel } from "@/components/panels/mcp-panel";

export default function McpPage() {
  return (
    <AppShell active="mcp">
      <McpPanel />
    </AppShell>
  );
}
