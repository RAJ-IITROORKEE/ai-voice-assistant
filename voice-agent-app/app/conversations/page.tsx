import { AppShell } from "@/components/app-shell";
import { ConversationsPanel } from "@/components/panels/conversations-panel";
import { AuthGate } from "@/components/auth-gate";

export default function ConversationsPage() {
  return (
    <AuthGate>
      <AppShell active="conversations">
        <ConversationsPanel />
      </AppShell>
    </AuthGate>
  );
}
