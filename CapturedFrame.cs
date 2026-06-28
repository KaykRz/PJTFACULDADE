namespace PSPLinkUsb;

internal readonly struct CapturedFrame
{
    public CapturedFrame(byte[] buffer, int length, int width = 480, int height = 272)
    {
        Buffer = buffer;
        Length = length;
        Width = width;
        Height = height;
    }

    public byte[] Buffer { get; }
    public int Length { get; }
    public int Width { get; }
    public int Height { get; }
}
