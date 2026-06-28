using System.Buffers.Binary;

namespace PSPLinkUsb;

public static class PspDispProtocol
{
    public const uint HeaderMagic = 0xAFFE0600;

    public const uint ContainsImageData = 0x00000001;
    public const uint ContainsSettingsData = 0x00000004;

    public const uint ImageIsJpeg = 0x00000010;
    public const uint ImageIsUncompressed = 0x00000040;

    public const uint ImageClearScreen = 0x00100000;
    public const uint ForceUpdate = 0x01000000;

    public const byte UsbOutEndpoint = 0x02;
    public const byte UsbInEndpoint = 0x81;

    public const ushort SonyVid = 0x054C;
    public const ushort PspTypeB = 0x01C9;
    public const ushort PspTypeC = 0x01CA;

    public static byte[] BuildJpegFrameHeader(int jpegSize)
    {
        if (jpegSize <= 0)
            throw new ArgumentOutOfRangeException(nameof(jpegSize));

        byte[] header = new byte[16];
        uint flags = ContainsImageData | ImageIsJpeg;

        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(0, 4), HeaderMagic);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(4, 4), flags);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(8, 4), (uint)jpegSize);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(12, 4), 0u);

        return header;
    }
}
