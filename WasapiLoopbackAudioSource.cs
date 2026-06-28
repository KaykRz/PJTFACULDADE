using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace PSPLinkUsb;

internal sealed class WasapiLoopbackAudioSource : IAudioPacketSource
{
    private const int TargetRate = TestAudioSource.SampleRate;
    private readonly WasapiLoopbackCapture _capture;
    private readonly Queue<short> _samples = new(TargetRate);
    private readonly object _lock = new();
    private readonly int _sourceRate;
    private readonly int _channels;
    private readonly int _bitsPerSample;
    private readonly WaveFormatEncoding _encoding;
    private readonly float _gain;
    private double _sourceFrameAccumulator;

    public WasapiLoopbackAudioSource(float gain)
    {
        MMDevice device = new MMDeviceEnumerator().GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
        _capture = new WasapiLoopbackCapture(device);
        _sourceRate = _capture.WaveFormat.SampleRate;
        _channels = Math.Max(1, _capture.WaveFormat.Channels);
        _bitsPerSample = _capture.WaveFormat.BitsPerSample;
        _encoding = _capture.WaveFormat.Encoding;
        _gain = Math.Clamp(gain, 0.25f, 3.0f);
        _capture.DataAvailable += OnDataAvailable;
        _capture.StartRecording();
    }

    public bool TryFillPacket(byte[] buffer)
    {
        lock (_lock)
        {
            int needed = TestAudioSource.PacketSamples;
            int offset = 0;
            while (offset < needed)
            {
                short sample = _samples.Count > 0 ? _samples.Dequeue() : (short)0;
                buffer[offset * 2 + 0] = (byte)(sample & 0xff);
                buffer[offset * 2 + 1] = (byte)((sample >> 8) & 0xff);
                offset++;
            }

            while (_samples.Count > TargetRate / 4)
                _samples.Dequeue();
            return true;
        }
    }

    public void Dispose()
    {
        _capture.DataAvailable -= OnDataAvailable;
        _capture.StopRecording();
        _capture.Dispose();
    }

    private void OnDataAvailable(object? sender, WaveInEventArgs e)
    {
        int bytesPerSample = Math.Max(1, _bitsPerSample / 8);
        int frameBytes = bytesPerSample * _channels;
        if (frameBytes <= 0)
            return;

        lock (_lock)
        {
            int sourceFrames = e.BytesRecorded / frameBytes;
            double sourceFramesPerTargetFrame = _sourceRate / (double)TargetRate;

            while (_sourceFrameAccumulator + 1.0 < sourceFrames)
            {
                int frame = (int)_sourceFrameAccumulator;
                double frac = _sourceFrameAccumulator - frame;
                int offset = frame * frameBytes;
                int nextOffset = (frame + 1) * frameBytes;

                float leftA = ReadSample(e.Buffer, offset, bytesPerSample);
                float leftB = ReadSample(e.Buffer, nextOffset, bytesPerSample);
                float left = leftA + (leftB - leftA) * (float)frac;

                _samples.Enqueue(ToInt16(left));
                _sourceFrameAccumulator += sourceFramesPerTargetFrame;
            }

            _sourceFrameAccumulator -= sourceFrames;
            if (_sourceFrameAccumulator < 0.0)
                _sourceFrameAccumulator = 0.0;

            while (_samples.Count > TargetRate / 3)
                _samples.Dequeue();
        }
    }

    private short ToInt16(float value)
    {
        float amplified = Math.Clamp(value, -1.0f, 1.0f) * 0.65f * _gain;
        amplified = MathF.Tanh(amplified * 1.2f) / MathF.Tanh(1.2f);
        return (short)Math.Clamp((int)(amplified * short.MaxValue), short.MinValue, short.MaxValue);
    }

    private float ReadSample(byte[] buffer, int offset, int bytesPerSample)
    {
        if (_encoding == WaveFormatEncoding.IeeeFloat && bytesPerSample == 4)
            return Math.Clamp(BitConverter.ToSingle(buffer, offset), -1.0f, 1.0f);

        if (bytesPerSample == 2)
            return BitConverter.ToInt16(buffer, offset) / 32768.0f;

        if (bytesPerSample == 4)
            return BitConverter.ToInt32(buffer, offset) / 2147483648.0f;

        if (bytesPerSample == 1)
            return (buffer[offset] - 128) / 128.0f;

        return 0.0f;
    }
}
