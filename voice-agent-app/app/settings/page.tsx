import { AppShell } from "@/components/app-shell";
import { SettingsPanel } from "@/components/panels/settings-panel";

export default function SettingsPage() {
  return (
    <AppShell active="settings">
      <SettingsPanel />
    </AppShell>
  );
}
