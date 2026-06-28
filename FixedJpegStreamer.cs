using System.Diagnostics;

namespace PSPLinkUsb;

internal static class FixedJpegStreamer
{
    public static void Run(string jpegPath, int fps, int responseBytes, bool verboseFrames)
    {
        byte[] jpeg = File.ReadAllBytes(jpegPath);
        ValidateJpeg(jpeg);

        byte[] header = PspDispProtocol.BuildJpegFrameHeader(jpeg.Length);

        Console.WriteLine("PSPdisp minimal fixed-JPEG USB test");
        Console.WriteLine($"JPEG: {Path.GetFullPath(jpegPath)}");
        Console.WriteLine($"jpeg_size: {jpeg.Length}");
        Console.WriteLine($"response_bytes: {responseBytes}");
        Console.WriteLine($"header bytes: {Hex(header, header.Length)}");
        Console.WriteLine($"jpeg first 16: {Hex(jpeg, Math.Min(16, jpeg.Length))}");
        Console.WriteLine($"jpeg last 16: {Hex(jpeg.AsSpan(jpeg.Length - Math.Min(16, jpeg.Length)).ToArray(), Math.Min(16, jpeg.Length))}");

        using UsbTransport usb = new();
        usb.Open(Console.WriteLine);
        Console.WriteLine("USB connected.");

        long frameTicks = Stopwatch.Frequency / Math.Clamp(fps, 1, 120);
        long nextTick = Stopwatch.GetTimestamp();
        long statsTick = nextTick;
        uint frames = 0;
        uint totalFrames = 0;

        while (true)
        {
            int headerWritten = usb.Write(header, header.Length);
            int jpegWritten = usb.Write(jpeg, jpeg.Length);
            byte[] response = usb.ReadExact(responseBytes, 300);

            frames++;
            totalFrames++;
            if (verboseFrames || totalFrames == 1)
            {
                Console.WriteLine($"frame {totalFrames}: out_header={headerWritten} out_jpeg={jpegWritten} in_response={response.Length}");
                Console.WriteLine($"response bytes: {Hex(response, response.Length)}");
            }

            long now = Stopwatch.GetTimestamp();
            if (now - statsTick >= Stopwatch.Frequency)
            {
                double seconds = (now - statsTick) / (double)Stopwatch.Frequency;
                Console.Write($"\rFPS {frames / seconds,5:0.0} | jpeg {jpeg.Length / 1024.0,5:0.0} KiB | header {Hex(header, header.Length)}   ");
                frames = 0;
                statsTick = now;
            }

            nextTick += frameTicks;
            while ((now = Stopwatch.GetTimestamp()) < nextTick)
            {
                double remainingMs = (nextTick - now) * 1000.0 / Stopwatch.Frequency;
                if (remainingMs > 2.0)
                    Thread.Sleep(1);
                else
                    Thread.SpinWait(100);
            }

            if (Stopwatch.GetTimestamp() - nextTick > frameTicks)
                nextTick = Stopwatch.GetTimestamp();
        }
    }

    private static void ValidateJpeg(byte[] jpeg)
    {
        if (jpeg.Length < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8)
            throw new InvalidOperationException("JPEG invalido: nao comeca com FF D8.");
        if (jpeg[^2] != 0xFF || jpeg[^1] != 0xD9)
            throw new InvalidOperationException("JPEG invalido: nao termina com FF D9.");
    }

    private static string Hex(byte[] data, int length)
    {
        return BitConverter.ToString(data, 0, length).Replace('-', ' ');
    }
}
