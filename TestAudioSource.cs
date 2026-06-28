namespace PSPLinkUsb;

internal sealed class TestAudioSource : IAudioPacketSource
{
    public const int PacketSamples = 2048;
    public const int PacketBytes = PacketSamples * 2;
    public const int SampleRate = 44100;
    private const double Frequency = 440.0;
    private int _sampleIndex;

    public void FillTone(byte[] buffer)
    {
        TryFillPacket(buffer);
    }

    public bool TryFillPacket(byte[] buffer)
    {
        for (int i = 0; i < PacketSamples; i++)
        {
            double t = (_sampleIndex++) / (double)SampleRate;
            short sample = (short)(Math.Sin(t * Frequency * Math.Tau) * 7000.0);
            int offset = i * 2;
            buffer[offset + 0] = (byte)(sample & 0xff);
            buffer[offset + 1] = (byte)((sample >> 8) & 0xff);
        }

        return true;
    }

    public void Dispose()
    {
    }
}
