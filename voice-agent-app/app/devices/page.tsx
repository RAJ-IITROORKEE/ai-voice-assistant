import { AppShell } from "@/components/app-shell";
import { DevicesPanel } from "@/components/panels/devices-panel";
import { AuthGate } from "@/components/auth-gate";

export default function DevicesPage() {
  return (
    <AuthGate>
      <AppShell active="devices">
        <DevicesPanel />
      </AppShell>
    </AuthGate>
  );
}
