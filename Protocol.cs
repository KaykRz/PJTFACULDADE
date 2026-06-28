using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace PSPLinkUsb;

internal static class Protocol
{
    public const uint Magic = 0xAFFE0600;
    public const uint ContainsImageData = 0x00000001;
    public const uint ContainsAudioData = 0x00000002;
    public const uint ContainsSettingsData = 0x00000004;
    public const uint ImageIsJpeg = 0x00000010;
    public const uint Plas432x240 = 0x00000100;
    public const uint Plas400x224 = 0x00000200;
    public const uint Plas368x208 = 0x00000400;
    public const uint Audio22050Hz = 0x00002000;
    public const uint Audio44100Hz = 0x00004000;
    public const uint AudioChunk2240 = 0x00010000;
    public const uint AudioChunk1120 = 0x00040000;
    public const uint AudioHasSequence = 0x00080000;
    public const uint AudioChunk1024Mono = 0x00200000;
    public const uint AudioChunk2048Mono = 0x00400000;
    public const uint RightAnalogFlag = 0x10000000;
    public const uint L2Flag = 0x20000000;
    public const uint R2Flag = 0x40000000;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct FrameHeader
    {
        public uint Magic;
        public uint Flags;
        public uint ImageSize;
        public uint SettingsSize;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct ResponseHeader
    {
        public uint Magic;
        public uint Flags;
        public uint Buttons;
        public byte AnalogX;
        public byte AnalogY;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Settings
    {
        public uint DeviceIndex;
        public uint DeviceUseSideShow;
        public uint ScreenMode;
        public uint ScreenRotation;
        public uint ScreenViewport;
        public uint Quality;
        public uint ScreenNoCompression;
        public uint UpdateInterval;
        public uint SoundEnabled;
        public uint SoundSampleRate;
        public uint ControlIndex;
        public uint ControlCount;
        public uint DisplayDeviceIndex;
        public uint DisplayDeviceCount;
        public uint DisplayEnable;
        public uint DisplayAutoselect;
        public uint PresetIndex;
        public uint PresetCount;
        public uint PresetDoSet;
    }

    public static byte[] ToBytes<T>(T value) where T : struct
    {
        int size = Marshal.SizeOf<T>();
        byte[] bytes = new byte[size];
        IntPtr ptr = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.StructureToPtr(value, ptr, false);
            Marshal.Copy(ptr, bytes, 0, size);
            return bytes;
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    public static Settings DefaultSettings() => new()
    {
        ScreenMode = 3,
        ScreenViewport = 0,
        Quality = 2,
        ScreenNoCompression = 0,
        UpdateInterval = 0,
        SoundEnabled = 0,
        DisplayEnable = 1,
        DisplayDeviceCount = 1,
        ControlCount = 1,
        PresetCount = 1
    };

    public static ResponseHeader ResponseFromBytes(byte[] bytes)
    {
        if (bytes.Length < 14)
            return default;

        return new ResponseHeader
        {
            Magic = BitConverter.ToUInt32(bytes, 0),
            Flags = BitConverter.ToUInt32(bytes, 4),
            Buttons = BitConverter.ToUInt32(bytes, 8),
            AnalogX = bytes[12],
            AnalogY = bytes[13]
        };
    }

    public static bool TryReadTuning(byte[] bytes, out int quality, out double sharpen, out int pspFps)
    {
        quality = 0;
        sharpen = 0.0;
        pspFps = 0;

        if (bytes.Length < 90 || BitConverter.ToUInt32(bytes, 0) != Magic)
            return false;

        int settingsOffset = Marshal.SizeOf<ResponseHeader>();
        quality = (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(settingsOffset + 5 * 4, 4));
        int sharpenPercent = (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(settingsOffset + 16 * 4, 4));
        pspFps = (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(settingsOffset + 18 * 4, 4));

        quality = Math.Clamp(quality, 1, 100);
        sharpen = Math.Clamp(sharpenPercent / 100.0, 0.0, 1.0);
        pspFps = Math.Clamp(pspFps, 0, 120);
        return true;
    }
}
