using Npgsql;

namespace LunaRelay;

/// <summary>Per-user assistant settings mirrored from the web app (settings table).</summary>
public sealed record DeviceSettings(string Model, string Voice, string Language, string Persona)
{
    /// <summary>Requested speech pipeline id (classic | azure-realtime | gemini-live). Empty = default.</summary>
    public string Pipeline { get; init; } = string.Empty;
}

/// <summary>
/// InsForge Postgres access for the relay: device ownership resolution, last_seen heartbeat,
/// and per-user settings. Uses the owner's user_id so RLS policies (auth.uid() = user_id) pass
/// when the agent (which sets request.jwt.claims) writes messages on the device's behalf.
/// The relay connects with the database owner credentials in DatabaseUrl, so it can read/write
/// any row; it never exposes data back to the device beyond the voice reply.
/// </summary>
public sealed class InsForgeStore : IAsyncDisposable
{
    private readonly NpgsqlDataSource _dataSource;
    private readonly ILogger<InsForgeStore> _logger;

    private InsForgeStore(NpgsqlDataSource dataSource, ILogger<InsForgeStore> logger)
    {
        _dataSource = dataSource;
        _logger = logger;
    }

    public static InsForgeStore Create(InsForgeOptions options, ILogger<InsForgeStore> logger)
    {
        var builder = new NpgsqlDataSourceBuilder(ToNpgsqlConnectionString(options.DatabaseUrl));
        return new InsForgeStore(builder.Build(), logger);
    }

    /// <summary>
    /// Npgsql accepts key=value connection strings, not postgresql:// URIs. Convert a URI of the
    /// form postgresql://user:pass@host:port/db?sslmode=require into Npgsql's key=value form.
    /// If the value is already in key=value form, pass it through unchanged.
    /// </summary>
    private static string ToNpgsqlConnectionString(string databaseUrl)
    {
        if (!databaseUrl.StartsWith("postgres", StringComparison.OrdinalIgnoreCase))
        {
            return databaseUrl; // already key=value
        }
        var uri = new Uri(databaseUrl);
        string[] userInfo = uri.UserInfo.Split(':', 2);
        var builder = new NpgsqlConnectionStringBuilder
        {
            Host = uri.Host,
            Port = uri.Port > 0 ? uri.Port : 5432,
            Username = Uri.UnescapeDataString(userInfo[0]),
            Password = userInfo.Length > 1 ? Uri.UnescapeDataString(userInfo[1]) : string.Empty,
            Database = uri.AbsolutePath.TrimStart('/'),
        };
        // Honor sslmode query param (InsForge requires SSL).
        string query = uri.Query.TrimStart('?');
        foreach (string pair in query.Split('&', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] kv = pair.Split('=', 2);
            if (kv.Length == 2 && kv[0].Equals("sslmode", StringComparison.OrdinalIgnoreCase) &&
                Enum.TryParse<SslMode>(kv[1], ignoreCase: true, out SslMode mode))
            {
                builder.SslMode = mode;
            }
        }
        return builder.ConnectionString;
    }

    /// <summary>
    /// Resolve the InsForge user that owns the given device, upsert the device row, and bump
    /// last_seen/online. Returns the owner's user_id, or null if no mapping exists yet.
    /// If the device is unclaimed and <paramref name="defaultOwnerId"/> is supplied, claim it.
    /// </summary>
    public async Task<string?> ResolveOwnerAndTouchAsync(
        string deviceId,
        string? firmware,
        string? defaultOwnerId,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await _dataSource.OpenConnectionAsync(cancellationToken);

        // Existing mapping?
        await using (var lookup = new NpgsqlCommand(
            "SELECT user_id FROM devices WHERE device_id = @did LIMIT 1", connection))
        {
            lookup.Parameters.AddWithValue("did", deviceId);
            object? result = await lookup.ExecuteScalarAsync(cancellationToken);
            if (result is Guid ownerId)
            {
                await TouchAsync(connection, deviceId, firmware, cancellationToken);
                return ownerId.ToString();
            }
        }

        // No mapping. Claim for the default owner if configured (single-user deployment).
        if (string.IsNullOrWhiteSpace(defaultOwnerId) || !Guid.TryParse(defaultOwnerId, out Guid owner))
        {
            _logger.LogWarning("Device {DeviceId} has no owner mapping and no default owner is configured", deviceId);
            return null;
        }
        await using (var upsert = new NpgsqlCommand(
            """
            INSERT INTO devices (user_id, name, device_id, firmware, last_seen, online)
            VALUES (@uid, @name, @did, @fw, now(), true)
            ON CONFLICT (device_id) DO UPDATE
              SET last_seen = now(), online = true, firmware = COALESCE(EXCLUDED.firmware, devices.firmware)
            """, connection))
        {
            upsert.Parameters.AddWithValue("uid", owner);
            upsert.Parameters.AddWithValue("name", "Luna device " + deviceId);
            upsert.Parameters.AddWithValue("did", deviceId);
            upsert.Parameters.AddWithValue("fw", (object?)firmware ?? DBNull.Value);
            await upsert.ExecuteNonQueryAsync(cancellationToken);
        }
        _logger.LogInformation("Claimed device {DeviceId} for user {UserId}", deviceId, owner);
        return owner.ToString();
    }

    private static async Task TouchAsync(
        NpgsqlConnection connection, string deviceId, string? firmware, CancellationToken cancellationToken)
    {
        await using var touch = new NpgsqlCommand(
            "UPDATE devices SET last_seen = now(), online = true, " +
            "firmware = COALESCE(@fw, firmware) WHERE device_id = @did", connection);
        touch.Parameters.AddWithValue("did", deviceId);
        touch.Parameters.AddWithValue("fw", (object?)firmware ?? DBNull.Value);
        await touch.ExecuteNonQueryAsync(cancellationToken);
    }

    /// <summary>Mark a device offline (best-effort, on disconnect).</summary>
    public async Task MarkOfflineAsync(string deviceId, CancellationToken cancellationToken)
    {
        try
        {
            await using NpgsqlConnection connection = await _dataSource.OpenConnectionAsync(cancellationToken);
            await using var cmd = new NpgsqlCommand(
                "UPDATE devices SET online = false WHERE device_id = @did", connection);
            cmd.Parameters.AddWithValue("did", deviceId);
            await cmd.ExecuteNonQueryAsync(cancellationToken);
        }
        catch (Exception exception)
        {
            _logger.LogDebug(exception, "Could not mark device {DeviceId} offline", deviceId);
        }
    }

    /// <summary>Load the owner's settings; null when the row is missing.</summary>
    public async Task<DeviceSettings?> LoadSettingsAsync(string userId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await _dataSource.OpenConnectionAsync(cancellationToken);
        await using var cmd = new NpgsqlCommand(
            "SELECT model, voice, language, persona, pipeline FROM settings WHERE user_id = @uid LIMIT 1", connection);
        cmd.Parameters.AddWithValue("uid", Guid.Parse(userId));
        await using NpgsqlDataReader reader = await cmd.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }
        return new DeviceSettings(
            reader.GetString(0), reader.GetString(1), reader.GetString(2), reader.GetString(3))
        {
            Pipeline = reader.IsDBNull(4) ? string.Empty : reader.GetString(4),
        };
    }

    public async ValueTask DisposeAsync() => await _dataSource.DisposeAsync();
}
