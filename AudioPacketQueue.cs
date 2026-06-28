using System.Buffers.Binary;
using System.Diagnostics;

namespace PSPLinkUsb;

internal sealed class AudioPacketQueue : IDisposable
{
    private const int MaxPackets = 12;
    private readonly IAudioPacketSource _source;
    private readonly Queue<byte[]> _packets = new(MaxPackets);
    private readonly object _lock = new();
    private readonly Thread _thread;
    private readonly byte[] _pcmBuffer = new byte[TestAudioSource.PacketBytes];
    private volatile bool _running = true;
    private uint _sequence;

    public AudioPacketQueue(IAudioPacketSource source)
    {
        _source = source;
        _thread = new Thread(Run)
        {
            IsBackground = true,
            Priority = ThreadPriority.AboveNormal,
            Name = "audio-packets"
        };
        _thread.Start();
    }

    public bool TryTake(out byte[] packet)
    {
        lock (_lock)
        {
            if (_packets.Count == 0)
            {
                packet = Array.Empty<byte>();
                return false;
            }

            packet = _packets.Dequeue();
            return true;
        }
    }

    public void Dispose()
    {
        _running = false;
        _thread.Join(500);
        _source.Dispose();
    }

    private void Run()
    {
        long packetTicks = Stopwatch.Frequency * TestAudioSource.PacketSamples / TestAudioSource.SampleRate;
        long nextTick = Stopwatch.GetTimestamp() + packetTicks;

        while (_running)
        {
            long now = Stopwatch.GetTimestamp();
            long sleepTicks = nextTick - now;
            if (sleepTicks > Stopwatch.Frequency / 1000)
                Thread.Sleep(1);
            else if (sleepTicks > 0)
                Thread.Yield();
            else
            {
                ProducePacket();
                nextTick += packetTicks;
                if (Stopwatch.GetTimestamp() - nextTick > packetTicks)
                    nextTick = Stopwatch.GetTimestamp() + packetTicks;
            }
        }
    }

    private void ProducePacket()
    {
        if (!_source.TryFillPacket(_pcmBuffer))
            Array.Clear(_pcmBuffer);

        byte[] packet = new byte[TestAudioSource.PacketBytes + 4];
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(0, 4), _sequence++);
        Buffer.BlockCopy(_pcmBuffer, 0, packet, 4, _pcmBuffer.Length);

        lock (_lock)
        {
            while (_packets.Count >= MaxPackets)
                _packets.Dequeue();
            _packets.Enqueue(packet);
        }
    }
}
