namespace LunaRelay;

public static class TtsDeliveryPolicy
{
    public static bool IsInitialBufferReady(int bufferedBytes, int minimumBytes, bool inputComplete) =>
        bufferedBytes > 0 && (bufferedBytes >= minimumBytes || inputComplete);
}
