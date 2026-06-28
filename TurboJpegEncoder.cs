using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace PSPLinkUsb;

internal sealed class TurboJpegEncoder : IDisposable
{
    private const int TjPfBgr = 1;

    private readonly IntPtr _handle;
    private readonly IntPtr _jpegBuffer;
    private readonly ulong _jpegBufferSize;
    private readonly JpegSubsampling _subsampling;

    public TurboJpegEncoder(int width, int height, JpegSubsampling subsampling)
    {
        _subsampling = subsampling;
        _handle = tjInitCompress();
        if (_handle == IntPtr.Zero)
            throw new InvalidOperationException("tjInitCompress failed.");

        _jpegBufferSize = tjBufSize(width, height, (int)_subsampling);
        _jpegBuffer = tjAlloc((int)_jpegBufferSize);
        if (_jpegBuffer == IntPtr.Zero)
            throw new InvalidOperationException("tjAlloc failed.");
    }

    public CapturedFrame Encode(Bitmap bitmap, byte[] destination, int quality)
    {
        Rectangle rect = new(0, 0, bitmap.Width, bitmap.Height);
        BitmapData data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        try
        {
            IntPtr jpegBuffer = _jpegBuffer;
            ulong jpegSize = _jpegBufferSize;
            int result = tjCompress2(
                _handle,
                data.Scan0,
                bitmap.Width,
                data.Stride,
                bitmap.Height,
                TjPfBgr,
                ref jpegBuffer,
                ref jpegSize,
                (int)_subsampling,
                quality,
                0);

            if (result != 0)
                throw new InvalidOperationException("tjCompress2 failed.");
            if (jpegSize > (ulong)destination.Length)
                throw new InvalidOperationException("JPEG output buffer too small.");

            Marshal.Copy(_jpegBuffer, destination, 0, (int)jpegSize);
            return new CapturedFrame(destination, (int)jpegSize, bitmap.Width, bitmap.Height);
        }
        finally
        {
            bitmap.UnlockBits(data);
        }
    }

    public void Dispose()
    {
        if (_jpegBuffer != IntPtr.Zero)
            tjFree(_jpegBuffer);
        if (_handle != IntPtr.Zero)
            tjDestroy(_handle);
    }

    [DllImport("turbojpeg.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tjInitCompress();

    [DllImport("turbojpeg.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int tjDestroy(IntPtr handle);

    [DllImport("turbojpeg.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern ulong tjBufSize(int width, int height, int jpegSubsamp);

    [DllImport("turbojpeg.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tjAlloc(int bytes);

    [DllImport("turbojpeg.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void tjFree(IntPtr buffer);

    [DllImport("turbojpeg.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int tjCompress2(
        IntPtr handle,
        IntPtr srcBuf,
        int width,
        int pitch,
        int height,
        int pixelFormat,
        ref IntPtr jpegBuf,
        ref ulong jpegSize,
        int jpegSubsamp,
        int jpegQual,
        int flags);
}
