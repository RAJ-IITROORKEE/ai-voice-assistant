using System.Security.Cryptography;
using System.Text;

namespace LunaRelay;

public interface IDeviceAuthenticator
{
    AccessPrincipal? Authenticate(string? presentedToken);
}

public sealed class StaticDeviceTokenAuthenticator(string expectedToken) : IDeviceAuthenticator
{
    public AccessPrincipal? Authenticate(string? presentedToken)
    {
        if (string.IsNullOrEmpty(presentedToken) || string.IsNullOrEmpty(expectedToken))
        {
            return null;
        }

        byte[] presented = Encoding.UTF8.GetBytes(presentedToken);
        byte[] expected = Encoding.UTF8.GetBytes(expectedToken);
        if (presented.Length != expected.Length ||
            !CryptographicOperations.FixedTimeEquals(presented, expected))
        {
            return null;
        }

        return new AccessPrincipal(
            AssistantActorKind.Device, CredentialIdentity.FromToken(expectedToken), null);
    }
}
