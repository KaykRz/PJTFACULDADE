namespace PSPLinkUsb;

internal interface IAudioPacketSource : IDisposable
{
    bool TryFillPacket(byte[] buffer);
}
