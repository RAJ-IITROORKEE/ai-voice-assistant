using System.Net;

namespace LunaRelay.Setup;

public sealed record LanAddressCandidate(bool IsUp, bool IsWireless, IPAddress Address);

public static class LanAddressSelector
{
    public static IPAddress Select(IEnumerable<LanAddressCandidate> candidates)
    {
        foreach (LanAddressCandidate candidate in candidates)
        {
            if (candidate.IsUp && candidate.IsWireless && IsUsable(candidate.Address))
            {
                return candidate.Address;
            }
        }

        foreach (LanAddressCandidate candidate in candidates)
        {
            if (candidate.IsUp && IsUsable(candidate.Address))
            {
                return candidate.Address;
            }
        }

        throw new InvalidOperationException("No active LAN IPv4 address was found.");
    }

    private static bool IsUsable(IPAddress address) =>
        address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork &&
        !IPAddress.IsLoopback(address) &&
        !address.ToString().StartsWith("169.254.", StringComparison.Ordinal);
}
