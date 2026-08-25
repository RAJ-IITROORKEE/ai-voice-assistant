using System.Security.Cryptography;
using System.Text;

namespace LunaRelay;

public static class DeviceAuthenticator
{
    public static bool IsAuthorized(string? presentedToken, string expectedToken)
    {
        if (string.IsNullOrEmpty(presentedToken) || string.IsNullOrEmpty(expectedToken))
        {
            return false;
        }

        byte[] presented = Encoding.UTF8.GetBytes(presentedToken);
        byte[] expected = Encoding.UTF8.GetBytes(expectedToken);
        return presented.Length == expected.Length &&
               CryptographicOperations.FixedTimeEquals(presented, expected);
    }
}
