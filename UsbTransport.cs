namespace PSPLinkUsb;

internal sealed class UsbTransport : IDisposable
{
    private IntPtr _context;
    private IntPtr _device;

    public void Open(Action<string>? log = null)
    {
        int result = LibUsbNative.Init(out _context);
        if (result < 0)
            throw new IOException($"libusb init failed: {result}");

        _device = LibUsbNative.OpenDeviceWithVidPid(_context, LibUsbNative.SonyVid, LibUsbNative.HostFsPid);
        if (_device == IntPtr.Zero)
            throw new IOException("PSP USB device not found. Start USB stream on the PSP first.");

        LibUsbNative.SetConfiguration(_device, 1);
        result = LibUsbNative.ClaimInterface(_device, 0);
        if (result < 0)
            throw new IOException($"USB claim interface failed: {result}");

        LibUsbNative.SetInterfaceAltSetting(_device, 0, 0);
        InitializeHostFs(log);
    }

    public unsafe int Write(byte[] data, int length, uint timeout = 10000)
    {
        int offset = 0;
        fixed (byte* basePtr = data)
        {
            while (offset < length)
            {
                int result = LibUsbNative.BulkTransfer(_device, LibUsbNative.EndpointOut, basePtr + offset, length - offset, out int transferred, timeout);
                if (result < 0 || transferred <= 0)
                    throw new IOException($"USB write failed: {result}");
                offset += transferred;
            }
        }

        return offset;
    }

    public unsafe byte[] ReadExact(int length, uint timeout = 300)
    {
        byte[] data = new byte[length];
        int offset = 0;
        fixed (byte* basePtr = data)
        {
            while (offset < length)
            {
                int result = LibUsbNative.BulkTransfer(_device, LibUsbNative.EndpointIn, basePtr + offset, length - offset, out int transferred, timeout);
                if (result < 0 || transferred <= 0)
                    throw new IOException($"USB read failed: {result}");
                offset += transferred;
            }
        }
        return data;
    }

    private void InitializeHostFs(Action<string>? log)
    {
        byte[] magic = { 0x12, 0x08, 0x2F, 0x78 };
        byte[] scratch = new byte[12];
        Buffer.BlockCopy(magic, 0, scratch, 0, magic.Length);

        int written = Write(magic, 4, 500);
        log?.Invoke($"handshake out magic: {written} bytes [{Hex(magic, magic.Length)}]");

        try
        {
            written = Write(scratch, 12, 500);
            log?.Invoke($"handshake out 12: {written} bytes; USBHostFS already initialized");
            return;
        }
        catch (IOException)
        {
            byte[] hello = ReadExact(12, 500);
            log?.Invoke($"handshake in challenge: 12 bytes [{Hex(hello, hello.Length)}]");
            written = Write(hello, 12, 500);
            log?.Invoke($"handshake out echo: {written} bytes");
        }
    }

    private static string Hex(byte[] data, int length)
    {
        return BitConverter.ToString(data, 0, length).Replace('-', ' ');
    }

    public void Dispose()
    {
        if (_device != IntPtr.Zero)
        {
            LibUsbNative.ReleaseInterface(_device, 0);
            LibUsbNative.Close(_device);
            _device = IntPtr.Zero;
        }

        if (_context != IntPtr.Zero)
        {
            LibUsbNative.Exit(_context);
            _context = IntPtr.Zero;
        }
    }
}
