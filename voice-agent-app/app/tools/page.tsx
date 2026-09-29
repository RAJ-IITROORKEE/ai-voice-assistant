import { AppShell } from "@/components/app-shell";
import { ToolsPanel } from "@/components/panels/tools-panel";

export default function ToolsPage() {
  return (
    <AppShell active="tools">
      <ToolsPanel />
    </AppShell>
  );
}
