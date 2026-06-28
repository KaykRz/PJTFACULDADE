using System.Runtime.InteropServices;

namespace PSPLinkUsb;

internal static partial class LibUsbNative
{
    public const ushort SonyVid = 0x054C;
    public const ushort HostFsPid = 0x01C9;
    public const byte EndpointOut = 0x02;
    public const byte EndpointIn = 0x81;

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_init")]
    public static partial int Init(out IntPtr context);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_exit")]
    public static partial void Exit(IntPtr context);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_open_device_with_vid_pid")]
    public static partial IntPtr OpenDeviceWithVidPid(IntPtr context, ushort vid, ushort pid);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_set_configuration")]
    public static partial int SetConfiguration(IntPtr device, int configuration);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_claim_interface")]
    public static partial int ClaimInterface(IntPtr device, int interfaceNumber);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_release_interface")]
    public static partial int ReleaseInterface(IntPtr device, int interfaceNumber);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_set_interface_alt_setting")]
    public static partial int SetInterfaceAltSetting(IntPtr device, int interfaceNumber, int alternateSetting);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_close")]
    public static partial void Close(IntPtr device);

    [LibraryImport("libusb-1.0", EntryPoint = "libusb_bulk_transfer")]
    public static unsafe partial int BulkTransfer(IntPtr device, byte endpoint, byte* data, int length, out int transferred, uint timeout);
}
