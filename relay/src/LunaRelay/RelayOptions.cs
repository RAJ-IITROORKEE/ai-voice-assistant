namespace LunaRelay;

public sealed class RelayOptions
{
    public int ListenPort { get; init; } = 7443;
    public bool UseTls { get; init; } = true;
    public string DeviceToken { get; init; } = string.Empty;
    public string CertificatePath { get; init; } = "certs/relay.pfx";
    public string CertificatePassword { get; init; } = string.Empty;
}

public sealed class AzureSpeechOptions
{
    public string Key { get; init; } = string.Empty;
    public string Region { get; init; } = string.Empty;
    public string Language { get; init; } = "en-IN";
    public string Voice { get; init; } = "en-IN-NeerjaNeural";
}

public sealed class AzureOpenAiOptions
{
    public string Endpoint { get; init; } = string.Empty;
    public string ApiKey { get; init; } = string.Empty;
    public string Model { get; init; } = string.Empty;
}

public sealed record RelayConfiguration(
    RelayOptions Relay,
    AzureSpeechOptions AzureSpeech,
    AzureOpenAiOptions AzureOpenAI)
{
    public void Validate()
    {
        Require(Relay.DeviceToken, "Relay:DeviceToken");
        if (Relay.UseTls)
        {
            Require(Relay.CertificatePath, "Relay:CertificatePath");
            Require(Relay.CertificatePassword, "Relay:CertificatePassword");
        }
        Require(AzureSpeech.Key, "AzureSpeech:Key");
        Require(AzureSpeech.Region, "AzureSpeech:Region");
        Require(AzureSpeech.Language, "AzureSpeech:Language");
        Require(AzureSpeech.Voice, "AzureSpeech:Voice");
        Require(AzureOpenAI.Endpoint, "AzureOpenAI:Endpoint");
        Require(AzureOpenAI.ApiKey, "AzureOpenAI:ApiKey");
        Require(AzureOpenAI.Model, "AzureOpenAI:Model");
        if (!Uri.TryCreate(AzureOpenAI.Endpoint, UriKind.Absolute, out _))
        {
            throw new InvalidOperationException("AzureOpenAI:Endpoint must be an absolute URL.");
        }
    }

    private static void Require(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException($"Missing required setting {name}.");
        }
    }
}
