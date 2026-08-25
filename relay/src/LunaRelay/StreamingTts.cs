using System.Threading.Channels;
using Microsoft.CognitiveServices.Speech;

namespace LunaRelay;

public sealed class StreamingTts(
    AzureSpeechOptions options,
    ILogger<StreamingTts> logger)
{
    private const int MaxQueuedAudioBytes = 4 * 1024 * 1024;
    private const int InitialDeliveryBufferBytes = 57_600; // 1.2 seconds of 24 kHz PCM16.

    public async Task SynthesizeAsync(
        IAsyncEnumerable<string> textChunks,
        Func<ReadOnlyMemory<byte>, CancellationToken, ValueTask> onPcm,
        CancellationToken cancellationToken)
    {
        string endpoint = $"wss://{options.Region}.tts.speech.microsoft.com/cognitiveservices/websocket/v2";
        SpeechConfig config = SpeechConfig.FromEndpoint(new Uri(endpoint), options.Key);
        config.SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat.Raw24Khz16BitMonoPcm);
        config.SetProperty(PropertyId.SpeechServiceConnection_SynthVoice, options.Voice);
        using var synthesizer = new SpeechSynthesizer(config, audioConfig: null);
        using var request = new SpeechSynthesisRequest(SpeechSynthesisRequestInputType.TextStream);
        var audio = Channel.CreateUnbounded<byte[]>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false,
        });
        var backlog = new TtsAudioBacklog(MaxQueuedAudioBytes);
        Exception? callbackError = null;

        void SetCallbackError(Exception exception) =>
            Interlocked.CompareExchange(ref callbackError, exception, null);

        EventHandler<SpeechSynthesisEventArgs> handler = (_, eventArgs) =>
        {
            byte[] pcm = eventArgs.Result.AudioData;
            if (pcm.Length == 0 || Volatile.Read(ref callbackError) is not null)
            {
                return;
            }

            byte[] copy = (byte[])pcm.Clone();
            if (!backlog.TryReserve(copy.Length))
            {
                SetCallbackError(new InvalidOperationException("TTS audio backlog exceeded its safety limit."));
            }
            else if (!audio.Writer.TryWrite(copy))
            {
                backlog.Release(copy.Length);
                SetCallbackError(new InvalidOperationException("TTS audio stream closed unexpectedly."));
            }
        };
        synthesizer.Synthesizing += handler;

        Task sender = Task.Run(async () =>
        {
            var initialPcm = new List<byte[]>();
            int initialBytes = 0;
            bool deliveryStarted = false;

            async Task DeliverAsync(byte[] pcm)
            {
                try
                {
                    await onPcm(pcm, cancellationToken);
                }
                finally
                {
                    backlog.Release(pcm.Length);
                }
            }

            await foreach (byte[] pcm in audio.Reader.ReadAllAsync(cancellationToken))
            {
                if (!deliveryStarted)
                {
                    initialPcm.Add(pcm);
                    initialBytes += pcm.Length;
                    if (!TtsDeliveryPolicy.IsInitialBufferReady(
                            initialBytes, InitialDeliveryBufferBytes, inputComplete: false))
                    {
                        continue;
                    }

                    foreach (byte[] initial in initialPcm)
                    {
                        await DeliverAsync(initial);
                    }
                    initialPcm.Clear();
                    deliveryStarted = true;
                }
                else
                {
                    await DeliverAsync(pcm);
                }
            }

            if (!deliveryStarted && TtsDeliveryPolicy.IsInitialBufferReady(
                    initialBytes, InitialDeliveryBufferBytes, inputComplete: true))
            {
                foreach (byte[] initial in initialPcm)
                {
                    await DeliverAsync(initial);
                }
            }
        }, cancellationToken);

        bool inputClosed = false;
        void CloseInput()
        {
            if (!inputClosed)
            {
                request.InputStream.Close();
                inputClosed = true;
            }
        }

        Task<SpeechSynthesisResult> synthesis = synthesizer.SpeakAsync(request);
        try
        {
            await foreach (string chunk in textChunks.WithCancellation(cancellationToken))
            {
                if (Volatile.Read(ref callbackError) is Exception error)
                {
                    throw error;
                }
                if (!string.IsNullOrEmpty(chunk))
                {
                    request.InputStream.Write(chunk);
                }
            }
            CloseInput();
            using SpeechSynthesisResult result = await synthesis.WaitAsync(cancellationToken);
            if (result.Reason == ResultReason.Canceled)
            {
                SpeechSynthesisCancellationDetails details =
                    SpeechSynthesisCancellationDetails.FromResult(result);
                throw new InvalidOperationException($"TTS failed: {details.ErrorCode}");
            }
            if (result.Reason != ResultReason.SynthesizingAudioCompleted)
            {
                throw new InvalidOperationException($"Unexpected TTS result: {result.Reason}");
            }
            if (Volatile.Read(ref callbackError) is Exception callbackFailure)
            {
                throw callbackFailure;
            }
        }
        catch
        {
            CloseInput();
            try
            {
                await synthesizer.StopSpeakingAsync();
            }
            catch (Exception exception)
            {
                logger.LogDebug(exception, "TTS cancellation cleanup failed");
            }
            throw;
        }
        finally
        {
            synthesizer.Synthesizing -= handler;
            audio.Writer.TryComplete(callbackError);
        }

        await sender;
    }
}
