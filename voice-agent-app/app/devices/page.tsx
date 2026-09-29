import { AppShell } from "@/components/app-shell";
import { DevicesPanel } from "@/components/panels/devices-panel";

export default function DevicesPage() {
  return (
    <AppShell active="devices">
      <DevicesPanel />
    </AppShell>
  );
}
