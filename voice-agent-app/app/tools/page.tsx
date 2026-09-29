import { AppShell } from "@/components/app-shell";
import { ToolsPanel } from "@/components/panels/tools-panel";
import { AuthGate } from "@/components/auth-gate";

export default function ToolsPage() {
  return (
    <AuthGate>
      <AppShell active="tools">
        <ToolsPanel />
      </AppShell>
    </AuthGate>
  );
}
