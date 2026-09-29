import { AppShell } from "@/components/app-shell";
import { McpPanel } from "@/components/panels/mcp-panel";
import { AuthGate } from "@/components/auth-gate";

export default function McpPage() {
  return (
    <AuthGate>
      <AppShell active="mcp">
        <McpPanel />
      </AppShell>
    </AuthGate>
  );
}
