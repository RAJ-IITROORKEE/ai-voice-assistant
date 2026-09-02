using System.Text;
using Microsoft.CognitiveServices.Speech;
using Microsoft.CognitiveServices.Speech.Audio;

namespace LunaRelay;

public sealed class StreamingSpeechTurn : IAsyncDisposable
{
    private readonly object _gate = new();
    private readonly AudioStreamFormat _format;
    private readonly PushAudioInputStream _input;
    private readonly AudioConfig _audioConfig;
    private readonly SpeechRecognizer _recognizer;
    private readonly StringBuilder _finalText = new();
    private readonly TaskCompletionSource _terminal =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private Exception? _error;
    private bool _closed;

    public StreamingSpeechTurn(
        AzureSpeechOptions options, string recognitionLanguage, Action<string> onPartial)
    {
        SpeechConfig config = SpeechConfig.FromSubscription(options.Key, options.Region);
        config.SpeechRecognitionLanguage = recognitionLanguage;
        _format = AudioStreamFormat.GetWaveFormatPCM(16_000, 16, 1);
        _input = AudioInputStream.CreatePushStream(_format);
        _audioConfig = AudioConfig.FromStreamInput(_input);
        _recognizer = new SpeechRecognizer(config, _audioConfig);

        _recognizer.Recognizing += (_, eventArgs) =>
        {
            if (!string.IsNullOrWhiteSpace(eventArgs.Result.Text))
            {
                onPartial(eventArgs.Result.Text);
            }
        };
        _recognizer.Recognized += (_, eventArgs) =>
        {
            if (eventArgs.Result.Reason != ResultReason.RecognizedSpeech ||
                string.IsNullOrWhiteSpace(eventArgs.Result.Text))
            {
                return;
            }

            lock (_gate)
            {
                if (_finalText.Length != 0)
                {
                    _finalText.Append(' ');
                }
                _finalText.Append(eventArgs.Result.Text);
            }
        };
        _recognizer.Canceled += (_, eventArgs) =>
        {
            if (eventArgs.Reason == CancellationReason.Error)
            {
                _error = new InvalidOperationException(
                    $"Speech recognition failed: {eventArgs.ErrorCode}");
            }
            _terminal.TrySetResult();
        };
        _recognizer.SessionStopped += (_, _) => _terminal.TrySetResult();
    }

    public Task StartAsync() => _recognizer.StartContinuousRecognitionAsync();

    public void Write(ReadOnlySpan<byte> pcm)
    {
        if ((pcm.Length & 1) != 0)
        {
            throw new ArgumentException("PCM16 input must end on a sample boundary.", nameof(pcm));
        }

        lock (_gate)
        {
            ObjectDisposedException.ThrowIf(_closed, this);
            _input.Write(pcm.ToArray());
        }
    }

    public async Task<string> CompleteAsync(CancellationToken cancellationToken)
    {
        CloseInput();
        await _terminal.Task.WaitAsync(TimeSpan.FromSeconds(20), cancellationToken);
        await _recognizer.StopContinuousRecognitionAsync();
        if (_error is not null)
        {
            throw _error;
        }

        lock (_gate)
        {
            if (_finalText.Length == 0)
            {
                throw new InvalidOperationException("No speech was recognized.");
            }
            return _finalText.ToString();
        }
    }

    public async Task CancelAsync()
    {
        CloseInput();
        try
        {
            await _recognizer.StopContinuousRecognitionAsync();
        }
        catch (Exception)
        {
            // Cancellation cleanup is best effort.
        }
        _terminal.TrySetResult();
    }

    private void CloseInput()
    {
        lock (_gate)
        {
            if (_closed)
            {
                return;
            }
            _closed = true;
            _input.Close();
        }
    }

    public async ValueTask DisposeAsync()
    {
        await CancelAsync();
        _recognizer.Dispose();
        _audioConfig.Dispose();
        _input.Dispose();
        _format.Dispose();
    }
}
