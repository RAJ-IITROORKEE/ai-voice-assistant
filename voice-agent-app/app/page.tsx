import { Assistant } from "./assistant";
import { AuthGate } from "@/components/auth-gate";

export default function Home() {
  return (
    <AuthGate>
      <Assistant />
    </AuthGate>
  );
}
