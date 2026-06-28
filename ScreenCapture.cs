using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace PSPLinkUsb;

internal sealed class ScreenCapture : IDisposable
{
    private readonly int _width;
    private readonly int _height;
    private readonly Bitmap _target;
    private readonly Graphics _targetGraphics;
    private readonly ImageCodecInfo _jpegCodec;
    private readonly EncoderParameters _encoderParameters;
    private readonly Rectangle _sourceRect;
    private readonly MemoryStream _stream = new(256 * 1024);
    private readonly byte[] _jpegBuffer = new byte[512 * 1024];
    private readonly IntPtr _desktopWindow;
    private readonly IntPtr _desktopDc;
    private readonly TurboJpegEncoder? _turboJpeg;
    private int _quality;
    private readonly ScaleMode _scaleMode;
    private readonly bool _useTurboJpeg;
    private readonly bool _drawCursor;
    private readonly int _cursorSize;
    private double _sharpen;
    private readonly byte[] _sharpBuffer;

    public ScreenCapture(long quality, JpegSubsampling subsampling, ScaleMode scaleMode, bool useTurboJpeg, bool drawCursor, int cursorSize, double zoom, double sharpen, int width = 480, int height = 272)
    {
        Rectangle screenRect = Screen.PrimaryScreen?.Bounds ?? new Rectangle(0, 0, 1920, 1080);
        _sourceRect = BuildCaptureRect(screenRect, zoom);
        _width = Math.Clamp(width, 320, 480);
        _height = Math.Clamp(height, 180, 272);
        _target = new Bitmap(_width, _height, PixelFormat.Format24bppRgb);
        _targetGraphics = Graphics.FromImage(_target);
        _desktopWindow = GetDesktopWindow();
        _desktopDc = GetWindowDC(_desktopWindow);
        _quality = checked((int)quality);
        _scaleMode = scaleMode;
        _useTurboJpeg = useTurboJpeg;
        _drawCursor = drawCursor;
        _cursorSize = Math.Clamp(cursorSize, 8, 64);
        _sharpen = Math.Clamp(sharpen, 0.0, 1.0);
        _sharpBuffer = new byte[_width * _height * 3 + _height * 8];
        try
        {
            _turboJpeg = _useTurboJpeg ? new TurboJpegEncoder(_width, _height, subsampling) : null;
        }
        catch
        {
            _turboJpeg = null;
        }
        _jpegCodec = ImageCodecInfo.GetImageEncoders().First(c => c.FormatID == ImageFormat.Jpeg.Guid);
        _encoderParameters = new EncoderParameters(1);
        _encoderParameters.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, quality);
    }

    public CapturedFrame CaptureJpeg()
    {
        IntPtr targetDc = _targetGraphics.GetHdc();
        try
        {
            SetStretchBltMode(targetDc, _scaleMode == ScaleMode.ColorOnColor ? StretchColorOnColor : StretchHalftone);
            SetBrushOrgEx(targetDc, 0, 0, IntPtr.Zero);
            StretchBlt(
                targetDc, 0, 0, _width, _height,
                _desktopDc, _sourceRect.Left, _sourceRect.Top, _sourceRect.Width, _sourceRect.Height,
                SrcCopy);
            if (_drawCursor)
                DrawCursor(targetDc);
        }
        finally
        {
            _targetGraphics.ReleaseHdc(targetDc);
        }

        if (_sharpen > 0.001)
            ApplySharpen();

        if (_useTurboJpeg && _turboJpeg is not null)
            return _turboJpeg.Encode(_target, _jpegBuffer, _quality);

        _stream.SetLength(0);
        _target.Save(_stream, _jpegCodec, _encoderParameters);
        return new CapturedFrame(_stream.GetBuffer(), checked((int)_stream.Length), _width, _height);
    }

    public void UpdateTuning(int quality, double sharpen)
    {
        quality = Math.Clamp(quality, 1, 100);
        sharpen = Math.Clamp(sharpen, 0.0, 1.0);

        if (_quality == quality && Math.Abs(_sharpen - sharpen) < 0.001)
            return;

        _quality = quality;
        _sharpen = sharpen;
        _encoderParameters.Param[0]?.Dispose();
        _encoderParameters.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, (long)_quality);
    }

    public void Dispose()
    {
        _encoderParameters.Dispose();
        _stream.Dispose();
        _turboJpeg?.Dispose();
        _targetGraphics.Dispose();
        _target.Dispose();
        ReleaseDC(_desktopWindow, _desktopDc);
    }

    private const int StretchHalftone = 4;
    private const int StretchColorOnColor = 3;
    private const int CursorShowing = 0x00000001;
    private const int DiNormal = 0x0003;
    private const uint SrcCopy = 0x00CC0020;

    private static Rectangle BuildCaptureRect(Rectangle screenRect, double zoom)
    {
        zoom = Math.Clamp(zoom, 1.0, 2.0);
        if (zoom <= 1.0001)
            return screenRect;

        const double targetAspect = 480.0 / 272.0;
        int width = (int)Math.Round(screenRect.Width / zoom);
        int height = (int)Math.Round(width / targetAspect);

        if (height > screenRect.Height / zoom)
        {
            height = (int)Math.Round(screenRect.Height / zoom);
            width = (int)Math.Round(height * targetAspect);
        }

        width = Math.Clamp(width, 480, screenRect.Width);
        height = Math.Clamp(height, 272, screenRect.Height);

        int left = screenRect.Left + (screenRect.Width - width) / 2;
        int top = screenRect.Top + (screenRect.Height - height) / 2;
        return new Rectangle(left, top, width, height);
    }

    private void DrawCursor(IntPtr targetDc)
    {
        CursorInfo cursorInfo = new()
        {
            CbSize = Marshal.SizeOf<CursorInfo>()
        };

        if (!GetCursorInfo(ref cursorInfo) || (cursorInfo.Flags & CursorShowing) == 0)
            return;

        if (!GetIconInfo(cursorInfo.Cursor, out IconInfo iconInfo))
            return;

        try
        {
            double scaleX = _width / (double)_sourceRect.Width;
            double scaleY = _height / (double)_sourceRect.Height;
            int systemCursorWidth = Math.Max(1, GetSystemMetrics(13));
            int systemCursorHeight = Math.Max(1, GetSystemMetrics(14));
            int hotX = (int)Math.Round(iconInfo.XHotspot * (_cursorSize / (double)systemCursorWidth));
            int hotY = (int)Math.Round(iconInfo.YHotspot * (_cursorSize / (double)systemCursorHeight));
            int x = (int)Math.Round((cursorInfo.ScreenPos.X - _sourceRect.Left) * scaleX) - hotX;
            int y = (int)Math.Round((cursorInfo.ScreenPos.Y - _sourceRect.Top) * scaleY) - hotY;

            if (x > -_cursorSize && y > -_cursorSize && x < _width && y < _height)
                DrawIconEx(targetDc, x, y, cursorInfo.Cursor, _cursorSize, _cursorSize, 0, IntPtr.Zero, DiNormal);
        }
        finally
        {
            if (iconInfo.MaskBitmap != IntPtr.Zero)
                DeleteObject(iconInfo.MaskBitmap);
            if (iconInfo.ColorBitmap != IntPtr.Zero)
                DeleteObject(iconInfo.ColorBitmap);
        }
    }

    private unsafe void ApplySharpen()
    {
        Rectangle rect = new(0, 0, _width, _height);
        BitmapData data = _target.LockBits(rect, ImageLockMode.ReadWrite, PixelFormat.Format24bppRgb);
        int stride = data.Stride;
        int amount = (int)Math.Round(_sharpen * 256.0);

        try
        {
            Marshal.Copy(data.Scan0, _sharpBuffer, 0, stride * _height);
            fixed (byte* source = _sharpBuffer)
            {
                byte* destination = (byte*)data.Scan0;
                for (int y = 1; y < _height - 1; y++)
                {
                    byte* dstRow = destination + y * stride;
                    byte* srcRow = source + y * stride;
                    byte* srcUp = source + (y - 1) * stride;
                    byte* srcDown = source + (y + 1) * stride;

                    for (int x = 1; x < _width - 1; x++)
                    {
                        int offset = x * 3;
                        for (int c = 0; c < 3; c++)
                        {
                            int i = offset + c;
                            int center = srcRow[i];
                            int blur = (srcRow[i - 3] + srcRow[i + 3] + srcUp[i] + srcDown[i]) >> 2;
                            int value = center + (((center - blur) * amount) >> 8);
                            dstRow[i] = (byte)Math.Clamp(value, 0, 255);
                        }
                    }
                }
            }
        }
        finally
        {
            _target.UnlockBits(data);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Point
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct CursorInfo
    {
        public int CbSize;
        public int Flags;
        public IntPtr Cursor;
        public Point ScreenPos;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct IconInfo
    {
        public bool IsIcon;
        public int XHotspot;
        public int YHotspot;
        public IntPtr MaskBitmap;
        public IntPtr ColorBitmap;
    }

    [DllImport("user32.dll")]
    private static extern IntPtr GetDesktopWindow();

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindowDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    private static extern int SetStretchBltMode(IntPtr hdc, int mode);

    [DllImport("gdi32.dll")]
    private static extern bool SetBrushOrgEx(IntPtr hdc, int x, int y, IntPtr previous);

    [DllImport("gdi32.dll")]
    private static extern bool StretchBlt(
        IntPtr hdcDest,
        int xDest,
        int yDest,
        int widthDest,
        int heightDest,
        IntPtr hdcSrc,
        int xSrc,
        int ySrc,
        int widthSrc,
        int heightSrc,
        uint rop);

    [DllImport("user32.dll")]
    private static extern bool GetCursorInfo(ref CursorInfo cursorInfo);

    [DllImport("user32.dll")]
    private static extern bool GetIconInfo(IntPtr icon, out IconInfo iconInfo);

    [DllImport("user32.dll")]
    private static extern bool DrawIconEx(IntPtr hdc, int x, int y, IntPtr icon, int width, int height, int step, IntPtr brush, int flags);

    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int index);

    [DllImport("gdi32.dll")]
    private static extern bool DeleteObject(IntPtr obj);
}
