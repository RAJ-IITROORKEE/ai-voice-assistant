import { AppShell } from "@/components/app-shell";
import { SettingsPanel } from "@/components/panels/settings-panel";
import { AuthGate } from "@/components/auth-gate";

export default function SettingsPage() {
  return (
    <AuthGate>
      <AppShell active="settings">
        <SettingsPanel />
      </AppShell>
    </AuthGate>
  );
}
