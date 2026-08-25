namespace LunaRelay;

// Keeps streaming synthesis bounded without dropping PCM when the device plays at real time.
public sealed class TtsAudioBacklog
{
    private readonly int _capacityBytes;
    private int _bytesQueued;

    public TtsAudioBacklog(int capacityBytes)
    {
        if (capacityBytes <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(capacityBytes));
        }

        _capacityBytes = capacityBytes;
    }

    public int BytesQueued => Volatile.Read(ref _bytesQueued);

    public bool TryReserve(int bytes)
    {
        if (bytes < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(bytes));
        }

        while (true)
        {
            int queued = Volatile.Read(ref _bytesQueued);
            if (bytes > _capacityBytes - queued)
            {
                return false;
            }

            if (Interlocked.CompareExchange(ref _bytesQueued, queued + bytes, queued) == queued)
            {
                return true;
            }
        }
    }

    public void Release(int bytes)
    {
        if (bytes < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(bytes));
        }

        while (true)
        {
            int queued = Volatile.Read(ref _bytesQueued);
            if (bytes > queued)
            {
                throw new InvalidOperationException("TTS backlog accounting underflow.");
            }

            if (Interlocked.CompareExchange(ref _bytesQueued, queued - bytes, queued) == queued)
            {
                return;
            }
        }
    }
}
