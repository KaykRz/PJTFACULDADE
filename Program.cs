using System.Diagnostics;
using System.Runtime.InteropServices;
using PSPLinkUsb;

int fps = ReadIntArg(args, "--fps", 60);
int quality = ReadIntArg(args, "--quality", 78);
JpegSubsampling subsampling = ReadSubsamplingArg(args, "--subsampling", JpegSubsampling.S420);
ScaleMode scaleMode = ReadScaleArg(args, "--scale", ScaleMode.Halftone);
ImagePreset preset = ReadPresetArg(args, "--preset", ImagePreset.Balanced);
string debugFramePath = ReadStringArg(args, "--debug-frame", "debug-frame.jpg");
string fixedJpegPath = ReadStringArg(args, "--fixed-jpeg", string.Empty);
int responseBytes = ReadIntArg(args, "--response-bytes", 90);
bool verboseFrames = HasArg(args, "--verbose-frames");
bool useTurboJpeg = HasArg(args, "--turbojpeg") && !HasArg(args, "--gdiplus-jpeg");
bool drawCursor = !HasArg(args, "--no-cursor");
int cursorSize = ReadIntArg(args, "--cursor-size", 22);
double zoom = ReadDoubleArg(args, "--zoom", 1.0);
bool controllerEnabled = HasArg(args, "--controller");
bool invertY = !HasArg(args, "--normal-y");
bool audioReal = HasArg(args, "--audio");
bool audioTest = HasArg(args, "--audio-test");
float audioGain = (float)ReadDoubleArg(args, "--audio-gain", 1.5);
double sharpen = ReadDoubleArg(args, "--sharpen", -1.0);
bool adaptiveQuality = !HasArg(args, "--fixed-quality");
bool streamSizeSet = HasArg(args, "--stream-size");
bool adaptivePlas = HasArg(args, "--adaptive-plas") && !streamSizeSet && !HasArg(args, "--fixed-plas");
bool sendSettings = true;
bool debugFrameSaved = false;
bool stopping = false;
int activeQuality = Math.Clamp(quality, 1, 100);
int adaptiveMaxQuality = Math.Clamp(quality, 1, 100);
int adaptiveMinQuality = ReadIntArg(args, "--min-quality", 58);
int baseAdaptiveMinQuality;
bool adaptiveMinQualitySet = HasArg(args, "--min-quality");
int lastPspFps = 0;
int stablePspFpsSeconds = 0;
TryEnableDpiAwareness();
ApplyPreset(preset, args, ref quality, ref subsampling, ref scaleMode, ref sharpen);
if (!adaptiveMinQualitySet && preset == ImagePreset.FsrQuality)
    adaptiveMinQuality = 66;
if (!adaptiveMinQualitySet && preset == ImagePreset.Plas)
    adaptiveMinQuality = 66;
if (!adaptiveMinQualitySet && preset == ImagePreset.PlasQuality)
    adaptiveMinQuality = 70;
if (!adaptiveMinQualitySet && preset == ImagePreset.PlasFast)
    adaptiveMinQuality = 56;
if (!adaptiveMinQualitySet && preset == ImagePreset.Readable)
    adaptiveMinQuality = 68;
activeQuality = Math.Clamp(quality, 1, 100);
adaptiveMaxQuality = activeQuality;
adaptiveMinQuality = Math.Clamp(adaptiveMinQuality, 40, adaptiveMaxQuality);
baseAdaptiveMinQuality = adaptiveMinQuality;
var (streamWidth, streamHeight) = StreamSizeForPreset(preset, args);
uint plasFlags = PlasFlagsForSize(streamWidth, streamHeight);
int plasLevel = PlasLevelForSize(streamWidth, streamHeight);
int lowFpsSeconds = 0;
int highFpsSeconds = 0;
timeBeginPeriod(1);
Thread.CurrentThread.Priority = ThreadPriority.Highest;
TrySetHighPriority();

Console.WriteLine("PSPLink USB PC v2");
Console.WriteLine($"Target {fps} FPS | preset {preset} | stream {streamWidth}x{streamHeight} | JPEG quality {quality} {(adaptiveQuality ? $"adaptive {adaptiveMinQuality}-{adaptiveMaxQuality}" : "fixed")} | encoder {(useTurboJpeg ? "TurboJPEG" : "GDI+")} | subsampling {SubsamplingName(subsampling)} | scale {scaleMode} | sharpen {sharpen:0.00} | cursor {(drawCursor ? cursorSize : 0)}px | zoom {zoom:0.00}x | controller {(controllerEnabled ? "ViGEm" : "off")} | audio {(audioReal ? $"wasapi-44100-mono gain {audioGain:0.00}" : audioTest ? "test-tone-44100-mono" : "off")} | Y {(invertY ? "inverted" : "normal")} | protocol PSPdisp");
Console.WriteLine($"First transmitted JPEG will be saved to: {Path.GetFullPath(debugFramePath)}");
Console.WriteLine("Start USB stream on the PSP, then run this app. Ctrl+C to stop.");
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    stopping = true;
};

ScreenCapture? capture = null;
try
{
    if (!string.IsNullOrWhiteSpace(fixedJpegPath))
    {
        FixedJpegStreamer.Run(fixedJpegPath, fps, responseBytes, verboseFrames);
        return;
    }

    using UsbTransport usb = new();
    capture = CreateCapture(quality, subsampling, scaleMode, useTurboJpeg, drawCursor, cursorSize, zoom, sharpen, streamWidth, streamHeight);
    using ControllerBridge? controller = controllerEnabled ? CreateControllerBridge(invertY) : null;
    using AudioPacketQueue? audioQueue = CreateAudioQueue(audioReal, audioTest, audioGain);

    if (audioQueue is not null)
        Thread.Sleep(250);

    usb.Open();
    Console.WriteLine("USB connected.");

    long frameTicks = Stopwatch.Frequency / Math.Clamp(fps, 1, 120);
    long nextTick = Stopwatch.GetTimestamp();
    uint frames = 0;
    uint sentFrames = 0;
    ulong totalBytes = 0;
    long statsTick = Stopwatch.GetTimestamp();

    while (!stopping)
    {
        CapturedFrame jpeg = capture.CaptureJpeg();
        if (!debugFrameSaved)
        {
            File.WriteAllBytes(debugFramePath, jpeg.Buffer.AsSpan(0, jpeg.Length).ToArray());
            debugFrameSaved = true;
        }

        byte[] settings = sendSettings ? Protocol.ToBytes(Protocol.DefaultSettings()) : Array.Empty<byte>();
        byte[] audioPacketBuffer = Array.Empty<byte>();
        bool sendAudio = audioQueue is not null && audioQueue.TryTake(out audioPacketBuffer);

        Protocol.FrameHeader header = new()
        {
            Magic = Protocol.Magic,
            Flags = Protocol.ContainsImageData | Protocol.ImageIsJpeg | plasFlags |
                    (sendAudio ? Protocol.ContainsAudioData | Protocol.Audio44100Hz | Protocol.AudioChunk2048Mono | Protocol.AudioHasSequence : 0) |
                    (sendSettings ? Protocol.ContainsSettingsData : 0),
            ImageSize = (uint)jpeg.Length,
            SettingsSize = (uint)settings.Length
        };

        usb.Write(Protocol.ToBytes(header), 16);
        usb.Write(jpeg.Buffer, jpeg.Length);
        if (sendAudio)
            usb.Write(audioPacketBuffer!, audioPacketBuffer!.Length);
        if (settings.Length > 0)
            usb.Write(settings, settings.Length);

        byte[] responseBytesBuffer = usb.ReadExact(90, 300);
        if (controller is not null)
        {
            Protocol.ResponseHeader response = Protocol.ResponseFromBytes(responseBytesBuffer);
            if (response.Magic == Protocol.Magic)
                controller.Update(response);
        }
        if (Protocol.TryReadTuning(responseBytesBuffer, out int pspQuality, out double pspSharpen, out int pspFps))
        {
            lastPspFps = pspFps;
            if (adaptivePlas && MaybeUpdatePlasLevel(pspFps, ref plasLevel, ref lowFpsSeconds, ref highFpsSeconds, out int newWidth, out int newHeight))
            {
                streamWidth = newWidth;
                streamHeight = newHeight;
                plasFlags = PlasFlagsForSize(streamWidth, streamHeight);
                capture.Dispose();
                capture = CreateCapture(activeQuality, subsampling, scaleMode, useTurboJpeg, drawCursor, cursorSize, zoom, pspSharpen, streamWidth, streamHeight);
            }

            if (adaptiveQuality)
            {
                int levelMinQuality = Math.Min(adaptiveMaxQuality, baseAdaptiveMinQuality + (plasLevel * 4));
                activeQuality = UpdateAdaptiveQuality(activeQuality, levelMinQuality, adaptiveMaxQuality, pspFps, ref stablePspFpsSeconds);
                capture.UpdateTuning(activeQuality, pspSharpen);
            }
            else
            {
                activeQuality = pspQuality;
                capture.UpdateTuning(pspQuality, pspSharpen);
            }
        }
        sendSettings = false;

        frames++;
        sentFrames++;
        totalBytes += (uint)jpeg.Length;

        long now = Stopwatch.GetTimestamp();
        if (now - statsTick >= Stopwatch.Frequency)
        {
            double seconds = (now - statsTick) / (double)Stopwatch.Frequency;
            double measuredFps = frames / seconds;
            double avgKiB = frames == 0 ? 0 : totalBytes / 1024.0 / frames;
            Console.Write($"\rFPS {measuredFps,5:0.0} | PSP {lastPspFps,3} | {streamWidth}x{streamHeight} | q {activeQuality,2} | avg {avgKiB,5:0.0} KiB | total {totalBytes / 1024 / 1024,5} MiB   ");
            frames = 0;
            totalBytes = 0;
            statsTick = now;
        }

        nextTick += frameTicks;
        while ((now = Stopwatch.GetTimestamp()) < nextTick && !stopping)
        {
            long remainingTicks = nextTick - now;
            double remainingMs = remainingTicks * 1000.0 / Stopwatch.Frequency;
            if (remainingMs > 2.0)
                Thread.Sleep(1);
            else
                Thread.SpinWait(100);
        }

        if (Stopwatch.GetTimestamp() - nextTick > frameTicks)
            nextTick = Stopwatch.GetTimestamp();
    }

    Console.WriteLine();
    Console.WriteLine("Transmission stopped.");
}
catch (IOException ex)
{
    Console.WriteLine();
    Console.WriteLine($"USB disconnected / transmission stopped: {ex.Message}");
}
finally
{
    capture?.Dispose();
    timeEndPeriod(1);
}

static ControllerBridge? CreateControllerBridge(bool invertY)
{
    try
    {
        ControllerBridge bridge = new(invertY);
        Console.WriteLine("ViGEm controller connected.");
        return bridge;
    }
    catch (Exception ex)
    {
        Console.WriteLine($"ViGEm controller unavailable: {ex.GetType().Name}: {ex.Message}");
        if (ex.InnerException is not null)
            Console.WriteLine($"ViGEm inner error: {ex.InnerException.GetType().Name}: {ex.InnerException.Message}");
        Console.WriteLine("Video will continue. Check ViGEmBus driver/service, then run again with --controller.");
        return null;
    }
}

static AudioPacketQueue? CreateAudioQueue(bool audioReal, bool audioTest, float audioGain)
{
    if (!audioReal && !audioTest)
        return null;

    IAudioPacketSource source = audioReal ? new WasapiLoopbackAudioSource(audioGain) : new TestAudioSource();
    return new AudioPacketQueue(source);
}

static ScreenCapture CreateCapture(int quality, JpegSubsampling subsampling, ScaleMode scaleMode, bool useTurboJpeg, bool drawCursor, int cursorSize, double zoom, double sharpen, int width, int height)
{
    return new ScreenCapture(Math.Clamp(quality, 1, 100), subsampling, scaleMode, useTurboJpeg, drawCursor, cursorSize, zoom, sharpen, width, height);
}

static int ReadIntArg(string[] args, string name, int fallback)
{
    for (int i = 0; i < args.Length - 1; i++)
    {
        if (args[i].Equals(name, StringComparison.OrdinalIgnoreCase) && int.TryParse(args[i + 1], out int value))
            return value;
    }
    return fallback;
}

static string ReadStringArg(string[] args, string name, string fallback)
{
    for (int i = 0; i < args.Length - 1; i++)
    {
        if (args[i].Equals(name, StringComparison.OrdinalIgnoreCase))
            return args[i + 1];
    }
    return fallback;
}

static double ReadDoubleArg(string[] args, string name, double fallback)
{
    for (int i = 0; i < args.Length - 1; i++)
    {
        if (args[i].Equals(name, StringComparison.OrdinalIgnoreCase) &&
            double.TryParse(args[i + 1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double value))
            return value;
    }
    return fallback;
}

static bool HasArg(string[] args, string name)
{
    return args.Any(arg => arg.Equals(name, StringComparison.OrdinalIgnoreCase));
}

static int UpdateAdaptiveQuality(int current, int min, int max, int pspFps, ref int stableSeconds)
{
    if (pspFps <= 0)
        return current;

    if (pspFps < 45)
    {
        stableSeconds = 0;
        return Math.Max(min, current - 5);
    }

    if (pspFps < 53)
    {
        stableSeconds = 0;
        return Math.Max(min, current - 3);
    }

    if (pspFps < 57)
    {
        stableSeconds = 0;
        return Math.Max(min, current - 1);
    }

    stableSeconds++;
    if (stableSeconds >= 3 && current < max)
    {
        stableSeconds = 0;
        return Math.Min(max, current + 1);
    }

    return current;
}

static JpegSubsampling ReadSubsamplingArg(string[] args, string name, JpegSubsampling fallback)
{
    string value = ReadStringArg(args, name, string.Empty);
    return value.ToLowerInvariant() switch
    {
        "444" or "4:4:4" => JpegSubsampling.S444,
        "422" or "4:2:2" => JpegSubsampling.S422,
        "420" or "4:2:0" => JpegSubsampling.S420,
        _ => fallback
    };
}

static ScaleMode ReadScaleArg(string[] args, string name, ScaleMode fallback)
{
    string value = ReadStringArg(args, name, string.Empty);
    return value.ToLowerInvariant() switch
    {
        "color" or "coloroncolor" or "fast" => ScaleMode.ColorOnColor,
        "halftone" or "aa" => ScaleMode.Halftone,
        _ => fallback
    };
}

static string SubsamplingName(JpegSubsampling subsampling) => subsampling switch
{
    JpegSubsampling.S444 => "4:4:4",
    JpegSubsampling.S422 => "4:2:2",
    _ => "4:2:0"
};

static ImagePreset ReadPresetArg(string[] args, string name, ImagePreset fallback)
{
    string value = ReadStringArg(args, name, string.Empty).Replace("-", "", StringComparison.Ordinal).ToLowerInvariant();
    return value switch
    {
        "performance" or "perf" => ImagePreset.Performance,
        "balanced" or "balance" => ImagePreset.Balanced,
        "fsr" or "fsrquality" or "qualityplus" or "quality" => ImagePreset.FsrQuality,
        "plas" => ImagePreset.Plas,
        "plasquality" or "plasq" => ImagePreset.PlasQuality,
        "plasfast" or "plasperf" => ImagePreset.PlasFast,
        "readable" or "text" => ImagePreset.Readable,
        "pixelart" or "pixel" => ImagePreset.PixelArt,
        "custom" => ImagePreset.Custom,
        _ => fallback
    };
}

static void ApplyPreset(ImagePreset preset, string[] args, ref int quality, ref JpegSubsampling subsampling, ref ScaleMode scaleMode, ref double sharpen)
{
    if (preset == ImagePreset.Custom)
    {
        if (sharpen < 0.0)
            sharpen = 0.0;
        return;
    }

    bool qualitySet = HasArg(args, "--quality");
    bool subsamplingSet = HasArg(args, "--subsampling");
    bool scaleSet = HasArg(args, "--scale");
    bool sharpenSet = HasArg(args, "--sharpen");

    switch (preset)
    {
        case ImagePreset.Performance:
            if (!qualitySet) quality = 75;
            if (!subsamplingSet) subsampling = JpegSubsampling.S420;
            if (!scaleSet) scaleMode = ScaleMode.Halftone;
            if (!sharpenSet) sharpen = 0.20;
            break;
        case ImagePreset.Readable:
            if (!qualitySet) quality = 82;
            if (!subsamplingSet) subsampling = JpegSubsampling.S422;
            if (!scaleSet) scaleMode = ScaleMode.Halftone;
            if (!sharpenSet) sharpen = 0.28;
            break;
        case ImagePreset.FsrQuality:
            if (!qualitySet) quality = 82;
            if (!subsamplingSet) subsampling = JpegSubsampling.S422;
            if (!scaleSet) scaleMode = ScaleMode.Halftone;
            if (!sharpenSet) sharpen = 0.52;
            break;
        case ImagePreset.Plas:
            if (!qualitySet) quality = 74;
            if (!subsamplingSet) subsampling = JpegSubsampling.S420;
            if (!scaleSet) scaleMode = ScaleMode.Halftone;
            if (!sharpenSet) sharpen = 0.24;
            break;
        case ImagePreset.PlasQuality:
            if (!qualitySet) quality = 80;
            if (!subsamplingSet) subsampling = JpegSubsampling.S422;
            if (!scaleSet) scaleMode = ScaleMode.Halftone;
            if (!sharpenSet) sharpen = 0.30;
            break;
        case ImagePreset.PlasFast:
            if (!qualitySet) quality = 68;
            if (!subsamplingSet) subsampling = JpegSubsampling.S420;
            if (!scaleSet) scaleMode = ScaleMode.ColorOnColor;
            if (!sharpenSet) sharpen = 0.16;
            break;
        case ImagePreset.PixelArt:
            if (!qualitySet) quality = 82;
            if (!subsamplingSet) subsampling = JpegSubsampling.S420;
            if (!scaleSet) scaleMode = ScaleMode.ColorOnColor;
            if (!sharpenSet) sharpen = 0.0;
            break;
        default:
            if (!qualitySet) quality = 82;
            if (!subsamplingSet) subsampling = JpegSubsampling.S420;
            if (!scaleSet) scaleMode = ScaleMode.Halftone;
            if (!sharpenSet) sharpen = 0.35;
            break;
    }
}

static (int Width, int Height) StreamSizeForPreset(ImagePreset preset, string[] args)
{
    string value = ReadStringArg(args, "--stream-size", string.Empty).ToLowerInvariant();
    if (!string.IsNullOrEmpty(value))
    {
        string[] parts = value.Split('x');
        if (parts.Length == 2 && int.TryParse(parts[0], out int width) && int.TryParse(parts[1], out int height))
            return (width, height);
    }

    return preset == ImagePreset.Plas || preset == ImagePreset.PlasQuality || preset == ImagePreset.PlasFast ? (432, 240) : (480, 272);
}

static uint PlasFlagsForSize(int width, int height)
{
    return (width, height) switch
    {
        (432, 240) => Protocol.Plas432x240,
        (400, 224) => Protocol.Plas400x224,
        (368, 208) => Protocol.Plas368x208,
        _ => 0
    };
}

static int PlasLevelForSize(int width, int height)
{
    return (width, height) switch
    {
        (432, 240) => 0,
        (400, 224) => 1,
        (368, 208) => 2,
        _ => 0
    };
}

static (int Width, int Height) PlasSizeForLevel(int level)
{
    return level switch
    {
        1 => (400, 224),
        2 => (368, 208),
        _ => (432, 240)
    };
}

static bool MaybeUpdatePlasLevel(int pspFps, ref int level, ref int lowSeconds, ref int highSeconds, out int width, out int height)
{
    width = 0;
    height = 0;
    if (pspFps <= 0)
        return false;

    if (pspFps < 50)
    {
        lowSeconds++;
        highSeconds = 0;
    }
    else if (pspFps >= 57)
    {
        highSeconds++;
        lowSeconds = 0;
    }
    else
    {
        lowSeconds = 0;
        highSeconds = 0;
    }

    if (lowSeconds >= 2 && level < 1)
    {
        level++;
        lowSeconds = 0;
        (width, height) = PlasSizeForLevel(level);
        return true;
    }

    if (highSeconds >= 5 && level > 0)
    {
        level--;
        highSeconds = 0;
        (width, height) = PlasSizeForLevel(level);
        return true;
    }

    return false;
}

static void TrySetHighPriority()
{
    try
    {
        Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.High;
    }
    catch
    {
        // Normal priority still works; this only helps frame pacing when Windows allows it.
    }
}

static void TryEnableDpiAwareness()
{
    try
    {
        if (!SetProcessDpiAwarenessContext(new IntPtr(-4)))
            SetProcessDPIAware();
    }
    catch
    {
    }
}

[DllImport("winmm.dll")]
static extern uint timeBeginPeriod(uint period);

[DllImport("winmm.dll")]
static extern uint timeEndPeriod(uint period);

[DllImport("user32.dll")]
static extern bool SetProcessDpiAwarenessContext(IntPtr dpiContext);

[DllImport("user32.dll")]
static extern bool SetProcessDPIAware();
