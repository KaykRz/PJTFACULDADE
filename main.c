#include <pspctrl.h>
#include <pspaudio.h>
#include <pspaudiolib.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <pspjpeg.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspimpose_driver.h>
#include <pspusb.h>
#include <psputility.h>
#include <psputility_avmodules.h>
#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>
#include <string.h>

#include "display.h"
#include "protocol.h"
#include "../../../psp/source/usbhostfs/usbasync.h"

PSP_MODULE_INFO("PSPLinkUSB", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1024);

#define USB_HOSTFSDRIVER_NAME "USBHostFSDriver"
#define USB_HOSTFSDRIVER_PID_TYPE_B 0x1C9
#define COM_IMAGE_BUFFER_SIZE (400 * 1024)
#define COM_AUDIO_MAX_SIZE (2688 * 4)
#define AUDIO_RING_FRAMES 8192
#define AUDIO_TARGET_FRAMES ((audioSampleRate * 170) / 1000)
#define AUDIO_MAX_FRAMES ((audioSampleRate * 260) / 1000)
#define AUDIO_OUTPUT_FRAMES 1024
#define UI_SOUND_OUTPUT_FRAMES 512
#define AUDIO_PREROLL_FRAMES ((audioSampleRate * 150) / 1000)
#define AUDIO_UNDERRUN_FRAMES ((audioSampleRate * 55) / 1000)
#ifndef PSP_CTRL_HOME
#define PSP_CTRL_HOME 0x00010000
#endif
#ifndef PSP_CTRL_VOLUP
#define PSP_CTRL_VOLUP 0x00100000
#endif
#ifndef PSP_CTRL_VOLDOWN
#define PSP_CTRL_VOLDOWN 0x00200000
#endif

static int running = 1;
static int usbReady = 0;
static int usbModuleId = -1;
static int jpegReady = 0;
static int jpegModuleLoaded = 0;
static int libjpegReady = 0;
static volatile int captureLocalInput = 0;
static SceUID decodeThreadId = -1;
static SceUID decodeSema = -1;
static volatile int decodeThreadRunning = 0;
static volatile int decodeBusy = 0;
static volatile int decodingFrame = -1;
static volatile int pendingFrame = -1;
static volatile unsigned int renderedFpsCounter = 0;
static volatile unsigned int pspFpsReport = 0;
static volatile unsigned int receivedFrameCounter = 0;
static volatile unsigned int decodedFrameCounter = 0;
static volatile unsigned int decodeFailCounter = 0;
static volatile int lastDecodeResult = 0;
static volatile unsigned int lastImageSize = 0;
static volatile unsigned int lastFlags = 0;
static struct AsyncEndpoint usbEndpoint;
static unsigned char imageBuffers[3][COM_IMAGE_BUFFER_SIZE] __attribute__((aligned(64)));
static unsigned char scratchBuffer[4096] __attribute__((aligned(64)));
static unsigned int pixelBuffers[2][480 * 272] __attribute__((aligned(64)));
static ComFrameHeader frameHeaders[3];
static int pixelIndex = 0;
static int receiveIndex = 0;
static ComSettingsResponse response __attribute__((aligned(64)));
static short audioRing[AUDIO_RING_FRAMES] __attribute__((aligned(64)));
static short audioOutput[AUDIO_OUTPUT_FRAMES] __attribute__((aligned(64)));
static volatile int audioReadIndex = 0;
static volatile int audioWriteIndex = 0;
static volatile int audioFrames = 0;
static volatile int audioRunning = 0;
static volatile int audioPaused = 0;
static volatile int audioPrimed = 0;
static volatile unsigned int audioSeqLast = 0;
static volatile unsigned int audioSeqExpected = 0;
static volatile unsigned int audioSeqStarted = 0;
static volatile unsigned int audioSeqHaveLast = 0;
static volatile unsigned int audioDuplicateSeq = 0;
static volatile unsigned int audioMissingSeq = 0;
static volatile unsigned int audioUnderruns = 0;
static volatile unsigned int audioOverruns = 0;
static volatile unsigned int audioLastBytes = 0;
static volatile unsigned int audioLastQueuedFrames = 0;
static SceUID audioThreadId = -1;
static SceUID audioLock = -1;
static int audioChannel = -1;
static int audioSampleRate = 44100;
static SceUID uiSoundThreadId = -1;
static int uiSoundChannel = -1;
static volatile int uiSoundRunning = 0;
static volatile const unsigned char* uiSoundRequest = NULL;
static volatile unsigned int uiSoundRequestSize = 0;
static short uiSoundOutput[UI_SOUND_OUTPUT_FRAMES] __attribute__((aligned(64)));

extern unsigned char soundUiConfirm_start[];
extern unsigned char soundUiConfirm_end[];
extern unsigned char soundUiDecline_start[];
extern unsigned char soundUiDecline_end[];
extern unsigned char soundUiNavigation_start[];
extern unsigned char soundUiNavigation_end[];

typedef struct
{
  unsigned int magic;
  int videoPreset;
  int quality;
  int sharpen;
  int batterySaver;
  int fpsOverlay;
  int audioVolume;
  int language;
  int clockMode;
  int controlsEnabled;
} AppConfig;

#define APP_CONFIG_MAGIC 0x4B4E4C51
#define CONFIG_PATH "ms0:/PSP/GAME/PSPLinkUSB/psplink.cfg"

#ifndef PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE
#define PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE 7
#endif

static AppConfig appConfig;

static int defaultLanguage(void)
{
  int language = 0;
  if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &language) == 0)
  {
    if (language == PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE)
      return 1;
  }
  return 2;
}

typedef struct
{
  struct jpeg_source_mgr sourceManager;
  const JOCTET* buffer;
  unsigned int bufferSize;
} LibjpegSourceManager;

static struct jpeg_decompress_struct libjpegInfo;
static struct jpeg_error_mgr libjpegError;
static LibjpegSourceManager libjpegSource;
static unsigned char libjpegRow[480 * 4] __attribute__((aligned(64)));

static void libjpegInitSource(j_decompress_ptr cinfo)
{
  (void)cinfo;
}

static boolean libjpegFillInputBuffer(j_decompress_ptr cinfo)
{
  (void)cinfo;
  return TRUE;
}

static void libjpegSkipInputData(j_decompress_ptr cinfo, long count)
{
  if (count > 0)
  {
    if ((size_t)count > cinfo->src->bytes_in_buffer)
      count = (long)cinfo->src->bytes_in_buffer;
    cinfo->src->next_input_byte += count;
    cinfo->src->bytes_in_buffer -= count;
  }
}

static void libjpegTermSource(j_decompress_ptr cinfo)
{
  (void)cinfo;
}

static int decodeJpegLibjpeg(unsigned char* input, unsigned int inputSize, unsigned int* pixels)
{
  LibjpegSourceManager* source = &libjpegSource;
  int ok = 0;

  if (!libjpegReady)
    return -1;

  source->buffer = input;
  source->bufferSize = inputSize;
  source->sourceManager.bytes_in_buffer = inputSize;
  source->sourceManager.next_input_byte = input;

  if (jpeg_read_header(&libjpegInfo, TRUE) != JPEG_HEADER_OK)
    return -2;

  libjpegInfo.do_fancy_upsampling = FALSE;
  libjpegInfo.do_block_smoothing = FALSE;
  libjpegInfo.dct_method = JDCT_FASTEST;

  if (jpeg_start_decompress(&libjpegInfo))
  {
    while (libjpegInfo.output_scanline < libjpegInfo.output_height && libjpegInfo.output_scanline < 272)
    {
      JSAMPROW row = libjpegRow;
      unsigned int x;
      unsigned char* dst = ((unsigned char*)pixels) + (libjpegInfo.output_scanline * 480 * 4);

      jpeg_read_scanlines(&libjpegInfo, &row, 1);
      for (x = 0; x < libjpegInfo.output_width && x < 480; x++)
      {
        dst[x * 4 + 0] = libjpegRow[x * libjpegInfo.output_components + 0];
        dst[x * 4 + 1] = libjpegRow[x * libjpegInfo.output_components + 1];
        dst[x * 4 + 2] = libjpegRow[x * libjpegInfo.output_components + 2];
        dst[x * 4 + 3] = 0xff;
      }
    }
    jpeg_finish_decompress(&libjpegInfo);
    ok = 1;
  }

  return ok ? 0 : -3;
}

static int exitCallback(int arg1, int arg2, void* common)
{
  (void)arg1;
  (void)arg2;
  (void)common;
  running = 0;
  if (usbReady)
    sceUsbDeactivate(USB_HOSTFSDRIVER_PID_TYPE_B);
  return 0;
}

static int callbackThread(SceSize args, void* argp)
{
  (void)args;
  (void)argp;
  int cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
  sceKernelRegisterExitCallback(cbid);
  sceKernelSleepThreadCB();
  return 0;
}

static void setupCallbacks(void)
{
  SceUID thid = sceKernelCreateThread("callbacks", callbackThread, 0x11, 0xFA0, 0, 0);
  if (thid >= 0)
    sceKernelStartThread(thid, 0, 0);
}

static void audioPopBlock(short* output, int frames)
{
  int i;

  if (audioLock >= 0)
    sceKernelWaitSema(audioLock, 1, NULL);

  for (i = 0; i < frames; i++)
  {
    if (audioFrames > 0)
    {
      output[i] = audioRing[audioReadIndex];
      audioReadIndex = (audioReadIndex + 1) % AUDIO_RING_FRAMES;
      audioFrames--;
    }
    else
    {
      output[i] = 0;
    }
  }

  if (audioLock >= 0)
    sceKernelSignalSema(audioLock, 1);
}

static int audioBufferedFrames(void)
{
  int frames = 0;

  if (audioLock >= 0)
    sceKernelWaitSema(audioLock, 1, NULL);
  frames = audioFrames;
  if (audioLock >= 0)
    sceKernelSignalSema(audioLock, 1);

  return frames;
}

static void audioClearBuffer(void)
{
  if (audioLock >= 0)
    sceKernelWaitSema(audioLock, 1, NULL);
  audioReadIndex = 0;
  audioWriteIndex = 0;
  audioFrames = 0;
  audioPrimed = 0;
  audioLastQueuedFrames = 0;
  if (audioLock >= 0)
    sceKernelSignalSema(audioLock, 1);
}

static void audioSetPaused(int paused)
{
  audioPaused = paused ? 1 : 0;
  audioClearBuffer();
}

static void audioOutputSilence(void)
{
  memset(audioOutput, 0, sizeof(audioOutput));
  if (audioChannel >= 0)
    sceAudioOutputBlocking(audioChannel, (PSP_AUDIO_VOLUME_MAX * appConfig.audioVolume) / 100, audioOutput);
}

static int audioThread(SceSize args, void* argp)
{
  (void)args;
  (void)argp;

  while (audioRunning)
  {
    if (audioPaused)
    {
      audioOutputSilence();
      continue;
    }

    if (!audioPrimed)
    {
      if (audioBufferedFrames() < AUDIO_PREROLL_FRAMES)
      {
        sceKernelDelayThread(2000);
        continue;
      }
      audioPrimed = 1;
    }

    if (audioBufferedFrames() < AUDIO_UNDERRUN_FRAMES)
    {
      audioPrimed = 0;
      audioUnderruns++;
      audioOutputSilence();
      continue;
    }

    audioPopBlock(audioOutput, AUDIO_OUTPUT_FRAMES);

    if (audioChannel >= 0)
      sceAudioOutputBlocking(audioChannel, (PSP_AUDIO_VOLUME_MAX * appConfig.audioVolume) / 100, audioOutput);
  }

  return 0;
}

static void startAudio(void)
{
  if (audioRunning)
    return;

  audioLock = sceKernelCreateSema("audioRing", 0, 1, 1, NULL);
  if (audioLock < 0)
    return;

  audioChannel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_OUTPUT_FRAMES, PSP_AUDIO_FORMAT_MONO);
  if (audioChannel < 0)
  {
    sceKernelDeleteSema(audioLock);
    audioLock = -1;
    return;
  }

  audioReadIndex = 0;
  audioWriteIndex = 0;
  audioFrames = 0;
  audioPrimed = 0;
  audioPaused = 0;
  audioSeqStarted = 0;
  audioSeqHaveLast = 0;
  audioDuplicateSeq = 0;
  audioMissingSeq = 0;
  audioUnderruns = 0;
  audioOverruns = 0;
  audioLastBytes = 0;
  audioLastQueuedFrames = 0;
  audioRunning = 1;
  audioThreadId = sceKernelCreateThread("audioThread", audioThread, 0x20, 0x2000, PSP_THREAD_ATTR_USER, 0);
  if (audioThreadId >= 0)
    sceKernelStartThread(audioThreadId, 0, NULL);
  else
  {
    audioRunning = 0;
    if (audioChannel >= 0)
      sceAudioChRelease(audioChannel);
    audioChannel = -1;
    sceKernelDeleteSema(audioLock);
    audioLock = -1;
  }
}

static void stopAudio(void)
{
  audioRunning = 0;
  if (audioThreadId >= 0)
  {
    sceKernelDelayThread(50000);
    sceKernelTerminateDeleteThread(audioThreadId);
    audioThreadId = -1;
  }
  if (audioChannel >= 0)
  {
    sceAudioChRelease(audioChannel);
    audioChannel = -1;
  }
  if (audioLock >= 0)
  {
    sceKernelDeleteSema(audioLock);
    audioLock = -1;
  }
  audioFrames = 0;
  audioPrimed = 0;
  audioPaused = 0;
}

static void audioSetSampleRate(int sampleRate)
{
  if (sampleRate == audioSampleRate)
    return;

  if (audioRunning)
    stopAudio();
  audioSampleRate = sampleRate;
}

static unsigned int readLe32(const unsigned char* data)
{
  return ((unsigned int)data[0]) |
         ((unsigned int)data[1] << 8) |
         ((unsigned int)data[2] << 16) |
         ((unsigned int)data[3] << 24);
}

static unsigned int readLe16(const unsigned char* data)
{
  return ((unsigned int)data[0]) |
         ((unsigned int)data[1] << 8);
}

static int findWavData(const unsigned char* wav, unsigned int size, const unsigned char** pcm, unsigned int* pcmBytes, unsigned int* channels)
{
  unsigned int offset = 12;
  unsigned int fmtChannels = 0;
  unsigned int fmtBits = 0;

  if (size < 44 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0)
    return 0;

  while (offset + 8 <= size)
  {
    unsigned int chunkSize = readLe32(wav + offset + 4);
    const unsigned char* chunk = wav + offset + 8;

    if (offset + 8 + chunkSize > size)
      break;

    if (memcmp(wav + offset, "fmt ", 4) == 0 && chunkSize >= 16)
    {
      if (readLe16(chunk) != 1)
        return 0;
      fmtChannels = readLe16(chunk + 2);
      fmtBits = readLe16(chunk + 14);
    }
    else if (memcmp(wav + offset, "data", 4) == 0)
    {
      if ((fmtChannels != 1 && fmtChannels != 2) || fmtBits != 16)
        return 0;
      *pcm = chunk;
      *pcmBytes = chunkSize;
      *channels = fmtChannels;
      return 1;
    }

    offset += 8 + chunkSize + (chunkSize & 1);
  }

  return 0;
}

static void requestUiSound(const unsigned char* start, const unsigned char* end)
{
  if (!uiSoundRunning || !start || !end || end <= start)
    return;

  uiSoundRequest = start;
  uiSoundRequestSize = (unsigned int)(end - start);
}

static int uiSoundThread(SceSize args, void* argp)
{
  (void)args;
  (void)argp;

  uiSoundChannel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, UI_SOUND_OUTPUT_FRAMES, PSP_AUDIO_FORMAT_MONO);
  if (uiSoundChannel < 0)
    return 0;

  while (uiSoundRunning)
  {
    const unsigned char* wav = (const unsigned char*)uiSoundRequest;
    unsigned int wavSize = uiSoundRequestSize;
    const unsigned char* pcm;
    unsigned int pcmBytes;
    unsigned int channels;
    unsigned int frame = 0;
    unsigned int totalFrames;

    if (!wav)
    {
      sceKernelDelayThread(2000);
      continue;
    }

    uiSoundRequest = NULL;
    uiSoundRequestSize = 0;
    if (!findWavData(wav, wavSize, &pcm, &pcmBytes, &channels))
      continue;

    totalFrames = pcmBytes / (channels * 2);
    while (uiSoundRunning && frame < totalFrames)
    {
      int i;
      for (i = 0; i < UI_SOUND_OUTPUT_FRAMES; i++)
      {
        if (frame < totalFrames)
        {
          const unsigned char* sample = pcm + frame * channels * 2;
          int left = (short)readLe16(sample);
          int value = left;

          if (channels == 2)
          {
            int right = (short)readLe16(sample + 2);
            value = (left + right) / 2;
          }

          uiSoundOutput[i] = (short)value;
          frame++;
        }
        else
        {
          uiSoundOutput[i] = 0;
        }
      }
      sceAudioOutputBlocking(uiSoundChannel, (PSP_AUDIO_VOLUME_MAX * appConfig.audioVolume) / 100, uiSoundOutput);

      if (uiSoundRequest)
        break;
    }
  }

  sceAudioChRelease(uiSoundChannel);
  uiSoundChannel = -1;
  return 0;
}

static void startUiSound(void)
{
  if (uiSoundRunning)
    return;

  uiSoundRunning = 1;
  uiSoundThreadId = sceKernelCreateThread("uiSound", uiSoundThread, 0x22, 0x3000, PSP_THREAD_ATTR_USER, 0);
  if (uiSoundThreadId >= 0)
    sceKernelStartThread(uiSoundThreadId, 0, NULL);
  else
    uiSoundRunning = 0;
}

static void stopUiSound(void)
{
  uiSoundRunning = 0;
  uiSoundRequest = NULL;
  uiSoundRequestSize = 0;
  if (uiSoundThreadId >= 0)
  {
    sceKernelTerminateDeleteThread(uiSoundThreadId);
    uiSoundThreadId = -1;
  }
  if (uiSoundChannel >= 0)
  {
    sceAudioChRelease(uiSoundChannel);
    uiSoundChannel = -1;
  }
}

static void frameDimensions(unsigned int flags, int* width, int* height)
{
  if (flags & COM_FLAGS_PLAS_432X240)
  {
    *width = 432;
    *height = 240;
  }
  else if (flags & COM_FLAGS_PLAS_400X224)
  {
    *width = 400;
    *height = 224;
  }
  else if (flags & COM_FLAGS_PLAS_368X208)
  {
    *width = 368;
    *height = 208;
  }
  else
  {
    *width = 480;
    *height = 272;
  }
}

static void audioPushPacket(const unsigned char* data, unsigned int bytes, int hasSequence)
{
  unsigned int frames = bytes / 2;
  unsigned int i;
  unsigned int seq = 0;

  if (!audioRunning)
    startAudio();
  if (audioPaused)
    return;
  if (!audioRunning || audioLock < 0)
    return;

  if (hasSequence)
  {
    if (bytes < 4)
      return;
    seq = readLe32(data);
    data += 4;
    bytes -= 4;
    frames = bytes / 2;

    if (!audioSeqStarted)
    {
      audioSeqStarted = 1;
      audioSeqExpected = seq;
    }

    if (audioSeqHaveLast && seq == audioSeqLast)
    {
      audioDuplicateSeq++;
      return;
    }

    if (seq != audioSeqExpected)
    {
      if (seq > audioSeqExpected)
      {
        audioMissingSeq += seq - audioSeqExpected;
        audioSeqExpected = seq;
      }
      else
      {
        audioDuplicateSeq++;
        return;
      }
    }

    audioSeqLast = seq;
    audioSeqHaveLast = 1;
    audioSeqExpected++;
  }

  audioLastBytes = bytes;

  sceKernelWaitSema(audioLock, 1, NULL);

  for (i = 0; i < frames; i++)
  {
    short sample = (short)(data[i * 2] | (data[i * 2 + 1] << 8));
    if (audioFrames >= AUDIO_RING_FRAMES)
    {
      audioReadIndex = (audioReadIndex + 1) % AUDIO_RING_FRAMES;
      audioFrames--;
    }
    audioRing[audioWriteIndex] = sample;
    audioWriteIndex = (audioWriteIndex + 1) % AUDIO_RING_FRAMES;
    audioFrames++;
  }
  audioLastQueuedFrames = audioFrames;

  if (audioFrames > AUDIO_MAX_FRAMES)
  {
    audioOverruns++;
    while (audioFrames > AUDIO_TARGET_FRAMES)
    {
      audioReadIndex = (audioReadIndex + 1) % AUDIO_RING_FRAMES;
      audioFrames--;
    }
    audioLastQueuedFrames = audioFrames;
  }

  sceKernelSignalSema(audioLock, 1);
}

static int readFull(void* buffer, int length)
{
  int done = 0;
  unsigned char* out = (unsigned char*)buffer;

  while (done < length && running)
  {
    int result = usbReadRawBulkData(out + done, length - done);
    if (result <= 0)
      return result;
    done += result;
  }

  return done;
}

static int discardBytes(int length)
{
  while (length > 0)
  {
    int chunk = (length > (int)sizeof(scratchBuffer)) ? (int)sizeof(scratchBuffer) : length;
    int result = readFull(scratchBuffer, chunk);
    if (result != chunk)
      return result;
    length -= chunk;
  }

  return 0;
}

static void fillSettings(ComSettings* settings)
{
  memset(settings, 0, sizeof(*settings));
  settings->screenMode = 3;
  settings->screenViewport = 0;
  settings->quality = appConfig.quality;
  settings->screenNoCompression = 0;
  settings->updateInterval = 0;
  settings->soundEnabled = 0;
  settings->displayEnable = 1;
  settings->displayDeviceCount = 1;
  settings->controlCount = 1;
  settings->presetIndex = appConfig.sharpen;
  settings->presetCount = 101;
  settings->presetDoSet = pspFpsReport;
}

static void configDefaults(void)
{
  appConfig.magic = APP_CONFIG_MAGIC;
  appConfig.videoPreset = 0;
  appConfig.quality = 84;
  appConfig.sharpen = 45;
  appConfig.batterySaver = 0;
  appConfig.fpsOverlay = 0;
  appConfig.audioVolume = 90;
  appConfig.language = 0;
  appConfig.clockMode = 0;
  appConfig.controlsEnabled = 1;
}

static void applyPowerConfig(void)
{
  if (appConfig.batterySaver)
    scePowerSetClockFrequency(300, 300, 150);
  else
    scePowerSetClockFrequency(333, 333, 166);
}

static void configSave(void)
{
  SceUID fd = sceIoOpen(CONFIG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0)
  {
    sceIoWrite(fd, &appConfig, sizeof(appConfig));
    sceIoClose(fd);
  }
}

static void configLoad(void)
{
  SceUID fd;
  configDefaults();
  fd = sceIoOpen(CONFIG_PATH, PSP_O_RDONLY, 0777);
  if (fd >= 0)
  {
    AppConfig loaded;
    int read = sceIoRead(fd, &loaded, sizeof(loaded));
    sceIoClose(fd);
    if (read == (int)sizeof(loaded) && loaded.magic == APP_CONFIG_MAGIC)
    {
      appConfig = loaded;
      if (appConfig.videoPreset < 0 || appConfig.videoPreset > 4)
        appConfig.videoPreset = 0;
      if (appConfig.quality < 1 || appConfig.quality > 100)
        appConfig.quality = 84;
      if (appConfig.sharpen < 0 || appConfig.sharpen > 100)
        appConfig.sharpen = 45;
      appConfig.batterySaver = appConfig.batterySaver ? 1 : 0;
      appConfig.fpsOverlay = appConfig.fpsOverlay ? 1 : 0;
      if (appConfig.audioVolume < 0 || appConfig.audioVolume > 100)
        appConfig.audioVolume = 90;
      if (appConfig.language < 0 || appConfig.language > 2)
        appConfig.language = 0;
      appConfig.clockMode = appConfig.clockMode ? 1 : 0;
      appConfig.controlsEnabled = appConfig.controlsEnabled ? 1 : 0;
    }
  }
}

static int configRowsForTab(int tab)
{
  if (tab == 0)
    return 3;
  return 1;
}

static void configAdjust(int tab, int row, int delta)
{
  if (tab == 0 && row == 0)
  {
    int current = appConfig.language ? appConfig.language : defaultLanguage();
    (void)delta;
    appConfig.language = (current == 2) ? 1 : 2;
  }
  else if (tab == 0 && row == 1)
  {
    appConfig.clockMode = !appConfig.clockMode;
  }
  else if (tab == 0 && row == 2)
  {
    appConfig.fpsOverlay = !appConfig.fpsOverlay;
  }
  else if (tab == 1 && row == 0)
  {
    appConfig.audioVolume += delta * 5;
    if (appConfig.audioVolume < 0)
      appConfig.audioVolume = 0;
    if (appConfig.audioVolume > 100)
      appConfig.audioVolume = 100;
    displaySetVolumeOverlay(appConfig.audioVolume);
  }
  else if (tab == 2 && row == 0)
  {
    appConfig.controlsEnabled = !appConfig.controlsEnabled;
  }
  configSave();
}

static int handleVolumeButtons(unsigned int pressed)
{
  int oldVolume = appConfig.audioVolume;

  if (pressed & PSP_CTRL_VOLUP)
    appConfig.audioVolume += 5;
  if (pressed & PSP_CTRL_VOLDOWN)
    appConfig.audioVolume -= 5;

  if (appConfig.audioVolume < 0)
    appConfig.audioVolume = 0;
  if (appConfig.audioVolume > 100)
    appConfig.audioVolume = 100;

  if (appConfig.audioVolume == oldVolume)
    return 0;

  displaySetVolumeOverlay(appConfig.audioVolume);
  configSave();
  return 1;
}

static void settingsLoop(void)
{
  int tab = 0;
  int row = 0;
  int dirty = 1;
  unsigned int lastButtons = 0;
  SceCtrlData ctrl;

  sceCtrlPeekBufferPositive(&ctrl, 1);
  lastButtons = ctrl.Buttons;
  while (running && (lastButtons & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_START)))
  {
    sceKernelDelayThread(16000);
    sceCtrlPeekBufferPositive(&ctrl, 1);
    lastButtons = ctrl.Buttons;
  }

  while (running)
  {
    unsigned int pressed;
    if (dirty)
    {
      displaySetPreferences(appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode);
      displaySettingsMenu(tab, row, appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode, appConfig.fpsOverlay, appConfig.audioVolume, appConfig.controlsEnabled);
      dirty = 0;
    }

    sceCtrlPeekBufferPositive(&ctrl, 1);
    pressed = ctrl.Buttons & ~lastButtons;
    lastButtons = ctrl.Buttons;

    if (handleVolumeButtons(pressed))
    {
      dirty = 1;
      continue;
    }

    if (pressed & PSP_CTRL_CIRCLE)
    {
      requestUiSound(soundUiDecline_start, soundUiDecline_end);
      break;
    }
    if (pressed & PSP_CTRL_LTRIGGER)
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      tab = (tab + 2) % 3;
      row = 0;
      dirty = 1;
    }
    else if (pressed & PSP_CTRL_RTRIGGER)
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      tab = (tab + 1) % 3;
      row = 0;
      dirty = 1;
    }
    if (pressed & PSP_CTRL_UP)
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      row = (row + configRowsForTab(tab) - 1) % configRowsForTab(tab);
      dirty = 1;
    }
    else if (pressed & PSP_CTRL_DOWN)
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      row = (row + 1) % configRowsForTab(tab);
      dirty = 1;
    }
    else if (pressed & PSP_CTRL_LEFT)
    {
      requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
      configAdjust(tab, row, -1);
      dirty = 1;
    }
    else if (pressed & PSP_CTRL_RIGHT)
    {
      requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
      configAdjust(tab, row, 1);
      dirty = 1;
    }
    else if (pressed & PSP_CTRL_CROSS)
    {
      requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
      configAdjust(tab, row, 1);
      dirty = 1;
    }

    sceKernelDelayThread(16000);
  }
}

static int connectPromptLoop(void)
{
  unsigned int lastButtons = 0;
  int dirty = 1;
  SceCtrlData ctrl;

  sceCtrlPeekBufferPositive(&ctrl, 1);
  lastButtons = ctrl.Buttons;

  while (running)
  {
    unsigned int pressed;

    if (dirty)
    {
      displayUsbWaiting(0, 0.0f);
      dirty = 0;
    }

    sceCtrlPeekBufferPositive(&ctrl, 1);
    pressed = ctrl.Buttons & ~lastButtons;
    lastButtons = ctrl.Buttons;

    if (handleVolumeButtons(pressed))
    {
      dirty = 1;
      continue;
    }
    if (pressed & PSP_CTRL_CIRCLE)
    {
      requestUiSound(soundUiDecline_start, soundUiDecline_end);
      return 0;
    }
    if (pressed & PSP_CTRL_CROSS)
    {
      requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
      return 1;
    }

    sceKernelDelayThread(16000);
  }

  return 0;
}

static void displayUsbConnectedSmooth(void)
{
  displayUsbWaiting(0, 0.0f);
}

static int sendResponse(void)
{
  SceCtrlData ctrl;
  int rightAnalogCombo;
  int l2Combo;
  int r2Combo;
  sceCtrlPeekBufferPositive(&ctrl, 1);

  rightAnalogCombo = (ctrl.Buttons & PSP_CTRL_SELECT) && (((int)ctrl.Lx < 104) || ((int)ctrl.Lx > 152) || ((int)ctrl.Ly < 104) || ((int)ctrl.Ly > 152));
  l2Combo = (ctrl.Buttons & PSP_CTRL_SELECT) && (ctrl.Buttons & PSP_CTRL_LTRIGGER);
  r2Combo = (ctrl.Buttons & PSP_CTRL_SELECT) && (ctrl.Buttons & PSP_CTRL_RTRIGGER);

  memset(&response, 0, sizeof(response));
  response.response.magic = COM_HEADER_MAGIC;

  if (captureLocalInput)
  {
    response.response.buttons = 0;
    response.response.analogX = 128;
    response.response.analogY = 128;
    fillSettings(&response.settings);
    sceKernelDcacheWritebackRange(&response, sizeof(response));
    return usbWriteRawBulkData(&response, sizeof(response));
  }

  if (appConfig.controlsEnabled)
  {
    response.response.buttons = ctrl.Buttons;
    if (rightAnalogCombo || l2Combo || r2Combo)
      response.response.buttons &= ~PSP_CTRL_SELECT;
    if (l2Combo)
    {
      response.response.buttons &= ~PSP_CTRL_LTRIGGER;
      response.response.flags |= PSPLINK_RESPONSE_L2;
    }
    if (r2Combo)
    {
      response.response.buttons &= ~PSP_CTRL_RTRIGGER;
      response.response.flags |= PSPLINK_RESPONSE_R2;
    }
    if (rightAnalogCombo)
      response.response.flags |= PSPLINK_RESPONSE_RIGHT_ANALOG;
    response.response.analogX = ctrl.Lx;
    response.response.analogY = ctrl.Ly;
  }
  else
  {
    response.response.buttons = 0;
    response.response.analogX = 128;
    response.response.analogY = 128;
  }
  fillSettings(&response.settings);

  sceKernelDcacheWritebackRange(&response, sizeof(response));
  return usbWriteRawBulkData(&response, sizeof(response));
}

static int startUsb(void)
{
  int result;

  usbModuleId = sceKernelLoadModule("usbhostfs.prx", 0, NULL);
  if (usbModuleId < 0)
    return -1;

  result = sceKernelStartModule(usbModuleId, 0, NULL, NULL, NULL);
  if (result < 0)
    return -2;

  result = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
  if (result < 0)
    return -3;

  result = sceUsbStart(USB_HOSTFSDRIVER_NAME, 0, 0);
  if (result < 0)
    return -4;

  result = sceUsbActivate(USB_HOSTFSDRIVER_PID_TYPE_B);
  if (result < 0)
    return -5;

  result = usbAsyncRegister(4, &usbEndpoint);
  if (result < 0)
    return -6;

  usbReady = 1;
  return 0;
}

static void stopUsb(void)
{
  if (usbReady)
  {
    usbAsyncUnregister(4);
    sceUsbDeactivate(USB_HOSTFSDRIVER_PID_TYPE_B);
  }

  sceUsbStop(USB_HOSTFSDRIVER_NAME, 0, 0);
  sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);

  if (usbModuleId >= 0)
  {
    sceKernelStopModule(usbModuleId, 0, NULL, NULL, NULL);
    sceKernelUnloadModule(usbModuleId);
  }

  usbReady = 0;
  usbModuleId = -1;
}

static int startJpeg(void)
{
  int result = sceJpegInitMJpeg();
  if (result == 0x8002013A)
  {
    int loadResult = sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);
    if (loadResult >= 0 || loadResult == 0x80111102)
      jpegModuleLoaded = 1;
    result = sceJpegInitMJpeg();
  }

  if (result < 0)
    return result;

  result = sceJpegCreateMJpeg(480, 272);
  if (result < 0)
    return result;

  memset(&libjpegSource, 0, sizeof(libjpegSource));
  libjpegInfo.err = jpeg_std_error(&libjpegError);
  jpeg_create_decompress(&libjpegInfo);
  libjpegSource.sourceManager.init_source = libjpegInitSource;
  libjpegSource.sourceManager.fill_input_buffer = libjpegFillInputBuffer;
  libjpegSource.sourceManager.skip_input_data = libjpegSkipInputData;
  libjpegSource.sourceManager.resync_to_restart = jpeg_resync_to_restart;
  libjpegSource.sourceManager.term_source = libjpegTermSource;
  libjpegInfo.src = (struct jpeg_source_mgr*)&libjpegSource;
  libjpegReady = 1;

  jpegReady = 1;
  return 0;
}

static void stopJpeg(void)
{
  if (!jpegReady)
    return;

  sceJpegDeleteMJpeg();
  sceJpegFinishMJpeg();
  if (libjpegReady)
    jpeg_destroy_decompress(&libjpegInfo);
  if (jpegModuleLoaded)
    sceUtilityUnloadAvModule(PSP_AV_MODULE_AVCODEC);
  jpegReady = 0;
  libjpegReady = 0;
  jpegModuleLoaded = 0;
}

static int decodeThread(SceSize args, void* argp)
{
  (void)args;
  (void)argp;

  while (decodeThreadRunning)
  {
    int index;
    unsigned int* pixels;
    int sourceWidth;
    int sourceHeight;

    sceKernelWaitSema(decodeSema, 1, NULL);
    if (!decodeThreadRunning)
      break;

    while ((index = pendingFrame) >= 0)
    {
      pendingFrame = -1;
      decodingFrame = index;
      pixelIndex = 1 - pixelIndex;
      pixels = pixelBuffers[pixelIndex];

      if ((frameHeaders[index].flags & COM_FLAGS_CONTAINS_IMAGE_DATA) &&
          (frameHeaders[index].flags & COM_FLAGS_IMAGE_IS_JPEG) &&
          frameHeaders[index].imageSize > 0)
      {
        lastDecodeResult = sceJpegDecodeMJpeg(imageBuffers[index], frameHeaders[index].imageSize, (unsigned char*)pixels, 0);
        if (lastDecodeResult < 0)
          lastDecodeResult = decodeJpegLibjpeg(imageBuffers[index], frameHeaders[index].imageSize, pixels);

        if (lastDecodeResult >= 0)
        {
          frameDimensions(frameHeaders[index].flags, &sourceWidth, &sourceHeight);
          displayFrame(pixels, sourceWidth, sourceHeight);
          renderedFpsCounter++;
          decodedFrameCounter++;
        }
        else
        {
          decodeFailCounter++;
        }
      }
      decodingFrame = -1;
    }

    decodeBusy = 0;
  }

  return 0;
}

static int startDecodeThread(void)
{
  decodeSema = sceKernelCreateSema("decodeSema", 0, 0, 1, NULL);
  if (decodeSema < 0)
    return decodeSema;

  decodeThreadRunning = 1;
  decodeThreadId = sceKernelCreateThread("decodeThread", decodeThread, 0x18, 0x10000, PSP_THREAD_ATTR_USER, 0);
  if (decodeThreadId < 0)
  {
    sceKernelDeleteSema(decodeSema);
    decodeSema = -1;
    decodeThreadRunning = 0;
    return decodeThreadId;
  }

  return sceKernelStartThread(decodeThreadId, 0, NULL);
}

static void stopDecodeThread(void)
{
  if (decodeThreadId < 0)
    return;

  decodeThreadRunning = 0;
  sceKernelSignalSema(decodeSema, 1);
  sceKernelWaitThreadEnd(decodeThreadId, NULL);
  sceKernelDeleteThread(decodeThreadId);
  sceKernelDeleteSema(decodeSema);
  decodeThreadId = -1;
  decodeSema = -1;
  decodeBusy = 0;
  decodingFrame = -1;
  pendingFrame = -1;
}

static int nextReceiveBuffer(void)
{
  int i;

  for (i = 0; i < 3; i++)
  {
    receiveIndex = (receiveIndex + 1) % 3;
    if (receiveIndex != decodingFrame)
      return receiveIndex;
  }

  return receiveIndex;
}

static int streamLoop(void)
{
  unsigned int fps = 0;
  unsigned int bad = 0;
  unsigned int gotFirstFrame = 0;
  int disconnected = 0;
  int streamMenuOpen = 0;
  int streamMenuSelected = 0;
  int streamMenuMode = 0;
  int streamMenuDirty = 0;
  int settingsTab = 0;
  int settingsRow = 0;
  unsigned int lastOverlayTick = 0;
  unsigned int lastFpsTick = sceKernelGetSystemTimeLow();
  unsigned int lastButtons = 0;

  renderedFpsCounter = 0;
  pspFpsReport = 0;
  receivedFrameCounter = 0;
  decodedFrameCounter = 0;
  decodeFailCounter = 0;
  lastDecodeResult = 0;
  lastImageSize = 0;
  lastFlags = 0;
  pendingFrame = -1;
  decodingFrame = -1;
  decodeBusy = 0;
  displayUsbWaiting(0, 0.0f);

  if (startJpeg() < 0)
  {
    int english = ((appConfig.language ? appConfig.language : defaultLanguage()) == 2);
    displayStatus("PSPLink USB", english ? "JPEG init failed." : "Falha ao iniciar JPEG.", english ? "Press O to go back" : "Aperte O para voltar");
    while (running)
    {
      SceCtrlData ctrl;
      sceCtrlPeekBufferPositive(&ctrl, 1);
      if (ctrl.Buttons & PSP_CTRL_CIRCLE)
        return 0;
      sceKernelDelayThread(16000);
    }
    return 0;
  }

  if (startDecodeThread() < 0)
  {
    int english = ((appConfig.language ? appConfig.language : defaultLanguage()) == 2);
    stopJpeg();
    displayStatus("PSPLink USB", english ? "Decode thread failed." : "Falha na thread de decode.", english ? "Press O to go back" : "Aperte O para voltar");
    while (running)
    {
      SceCtrlData ctrl;
      sceCtrlPeekBufferPositive(&ctrl, 1);
      if (ctrl.Buttons & PSP_CTRL_CIRCLE)
        return 0;
      sceKernelDelayThread(16000);
    }
    return 0;
  }

  while (running && usbReady)
  {
    ComFrameHeader header;
    SceCtrlData ctrl;
    unsigned int audioSize = 0;
    unsigned int audioPayloadSize = 0;

    unsigned int pressed;

    sceCtrlPeekBufferPositive(&ctrl, 1);
    pressed = ctrl.Buttons & ~lastButtons;

    if (handleVolumeButtons(pressed))
    {
      streamMenuDirty = 1;
      if (streamMenuOpen)
      {
        lastButtons = ctrl.Buttons;
        continue;
      }
    }

    if ((pressed & PSP_CTRL_HOME) || ((ctrl.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_START)) == (PSP_CTRL_SELECT | PSP_CTRL_START) &&
        ((lastButtons & (PSP_CTRL_SELECT | PSP_CTRL_START)) != (PSP_CTRL_SELECT | PSP_CTRL_START))))
    {
      streamMenuOpen = 1;
      streamMenuMode = 0;
      streamMenuSelected = 0;
      streamMenuDirty = 1;
      pendingFrame = -1;
      audioSetPaused(1);
    }

    if (streamMenuOpen)
    {
      captureLocalInput = 1;
      if (streamMenuMode == 0)
      {
        if (pressed & PSP_CTRL_UP)
        {
          requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
          streamMenuSelected = (streamMenuSelected + 2) % 3;
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_DOWN)
        {
          requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
          streamMenuSelected = (streamMenuSelected + 1) % 3;
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_CIRCLE)
        {
          requestUiSound(soundUiDecline_start, soundUiDecline_end);
          streamMenuOpen = 0;
          streamMenuDirty = 0;
          audioSetPaused(0);
        }
        else if (pressed & PSP_CTRL_CROSS)
        {
          requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
          if (streamMenuSelected == 0)
          {
            streamMenuOpen = 0;
            streamMenuDirty = 0;
            audioSetPaused(0);
          }
          else if (streamMenuSelected == 1)
          {
            streamMenuMode = 1;
            settingsTab = 0;
            settingsRow = 0;
            streamMenuDirty = 1;
          }
          else
          {
            audioSetPaused(0);
            break;
          }
        }
      }
      else
      {
        if (pressed & PSP_CTRL_CIRCLE)
        {
          requestUiSound(soundUiDecline_start, soundUiDecline_end);
          streamMenuMode = 0;
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_UP)
        {
          requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
          settingsRow = (settingsRow + configRowsForTab(settingsTab) - 1) % configRowsForTab(settingsTab);
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_DOWN)
        {
          requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
          settingsRow = (settingsRow + 1) % configRowsForTab(settingsTab);
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_LTRIGGER)
        {
          requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
          settingsTab = (settingsTab + 2) % 3;
          settingsRow = 0;
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_RTRIGGER)
        {
          requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
          settingsTab = (settingsTab + 1) % 3;
          settingsRow = 0;
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_LEFT)
        {
          requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
          configAdjust(settingsTab, settingsRow, -1);
          displaySetPreferences(appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode);
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_RIGHT)
        {
          requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
          configAdjust(settingsTab, settingsRow, 1);
          displaySetPreferences(appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode);
          streamMenuDirty = 1;
        }
        else if (pressed & PSP_CTRL_CROSS)
        {
          requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
          configAdjust(settingsTab, settingsRow, 1);
          displaySetPreferences(appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode);
          streamMenuDirty = 1;
        }
      }
    }
    else
    {
      captureLocalInput = 0;
      if (audioPaused)
        audioSetPaused(0);
    }

    lastButtons = ctrl.Buttons;

    if (readFull(&header, sizeof(header)) != (int)sizeof(header))
    {
      if (gotFirstFrame)
      {
        disconnected = 1;
        break;
      }
      bad++;
      sceKernelDelayThread(10000);
      continue;
    }

    if (header.magic != COM_HEADER_MAGIC)
    {
      bad++;
      continue;
    }

    if (header.flags & COM_FLAGS_CONTAINS_AUDIO_DATA)
    {
      if (header.flags & COM_FLAGS_AUDIO_44100_HZ)
        audioSetSampleRate(44100);
      else if (header.flags & COM_FLAGS_AUDIO_22050_HZ)
        audioSetSampleRate(22050);

      if (header.flags & COM_FLAGS_AUDIO_CHUNK_2048_MONO)
        audioSize = 2048 * 2;
      else if (header.flags & COM_FLAGS_AUDIO_CHUNK_1024_MONO)
        audioSize = 1024 * 2;
      else if (header.flags & COM_FLAGS_AUDIO_CHUNK_1120)
        audioSize = 1120 * 2;
      else if (header.flags & COM_FLAGS_AUDIO_CHUNK_2240)
        audioSize = 2240 * 2;
      else
        audioSize = COM_AUDIO_MAX_SIZE;

      audioPayloadSize = audioSize;
      if (header.flags & COM_FLAGS_AUDIO_HAS_SEQUENCE)
        audioPayloadSize += 4;
    }

    if (header.imageSize + header.settingsSize + audioPayloadSize > COM_IMAGE_BUFFER_SIZE)
    {
      bad++;
      discardBytes(header.imageSize + header.settingsSize + audioPayloadSize);
      sendResponse();
      continue;
    }

    receiveIndex = nextReceiveBuffer();
    if (readFull(imageBuffers[receiveIndex], header.imageSize + audioPayloadSize + header.settingsSize) !=
        (int)(header.imageSize + audioPayloadSize + header.settingsSize))
    {
      disconnected = 1;
      break;
    }

    sceKernelDcacheWritebackInvalidateRange(imageBuffers[receiveIndex], header.imageSize + audioPayloadSize + header.settingsSize);

    if (audioPayloadSize > 0)
      audioPushPacket(imageBuffers[receiveIndex] + header.imageSize, audioPayloadSize, (header.flags & COM_FLAGS_AUDIO_HAS_SEQUENCE) != 0);

    if (!streamMenuOpen &&
        (header.flags & COM_FLAGS_CONTAINS_IMAGE_DATA) &&
        (header.flags & COM_FLAGS_IMAGE_IS_JPEG) &&
        header.imageSize > 0)
    {
      if (!gotFirstFrame)
      {
        int english = ((appConfig.language ? appConfig.language : defaultLanguage()) == 2);
        gotFirstFrame = 1;
        bad = 0;
        displayStatus("PSPLink USB", english ? "Frame received. Decoding..." : "Frame recebido. Decodificando...", "HOME: menu");
      }
      lastImageSize = header.imageSize;
      lastFlags = header.flags;
      receivedFrameCounter++;
      frameHeaders[receiveIndex] = header;
      pendingFrame = receiveIndex;
      if (!decodeBusy)
      {
        decodeBusy = 1;
        sceKernelSignalSema(decodeSema, 1);
      }
    }

    sendResponse();

    if (streamMenuOpen)
    {
      unsigned int nowOverlay = sceKernelGetSystemTimeLow();
      if (streamMenuDirty || nowOverlay - lastOverlayTick >= 250000)
      {
        if (streamMenuMode == 0)
          displayStreamMenu(streamMenuSelected);
        else
        {
          displaySetPreferences(appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode);
          displaySettingsMenu(settingsTab, settingsRow, appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode, appConfig.fpsOverlay, appConfig.audioVolume, appConfig.controlsEnabled);
        }
        lastOverlayTick = nowOverlay;
        streamMenuDirty = 0;
      }
    }

    {
      unsigned int nowFps = sceKernelGetSystemTimeLow();
      if (nowFps - lastFpsTick >= 1000000)
      {
        fps = renderedFpsCounter;
        pspFpsReport = fps;
        renderedFpsCounter = 0;
        lastFpsTick = nowFps;
      }
    }

    displaySetFpsOverlay(appConfig.fpsOverlay, fps);

    if (gotFirstFrame && ((receivedFrameCounter & 31) == 0) && decodedFrameCounter == 0)
    {
      char line[96];
      char hint[96];
      snprintf(line, sizeof(line), "rx %u dec %u fail %u jpg 0x%08X",
               receivedFrameCounter, decodedFrameCounter, decodeFailCounter, lastDecodeResult);
      snprintf(hint, sizeof(hint), "size %u flags 0x%08X bad %u", lastImageSize, lastFlags, bad);
      displayStatus("PSPLink USB", line, hint);
    }

  }

  stopDecodeThread();
  stopAudio();
  captureLocalInput = 0;
  stopJpeg();
  return disconnected;
}

int main(int argc, char* argv[])
{
  int menuSelected = 0;
  int menuSidebarOpen = 0;
  int menuSidebarSelected = 0;
  int menuDirty = 1;
  unsigned int lastButtons = 0;
  unsigned int lastMenuClock = 0;
  char menuMessage[96] = "";

  (void)argc;
  (void)argv;

  setupCallbacks();
  sceImposeSetHomePopup(0);
  configLoad();
  displaySetPreferences(appConfig.language ? appConfig.language : defaultLanguage(), appConfig.clockMode);
  applyPowerConfig();
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
  displayInit();
  startUiSound();

  while (running)
  {
    SceCtrlData ctrl;
    unsigned int pressed;
    unsigned int now = sceKernelGetSystemTimeLow();

    if (now - lastMenuClock >= 1000000)
    {
      menuDirty = 1;
      lastMenuClock = now;
    }

    if (menuDirty)
    {
      displayMainMenuEx(menuSelected, menuSidebarOpen, menuSidebarSelected, menuMessage);
      menuDirty = 0;
    }

    sceCtrlPeekBufferPositive(&ctrl, 1);
    pressed = ctrl.Buttons & ~lastButtons;
    lastButtons = ctrl.Buttons;

    if (handleVolumeButtons(pressed))
    {
      menuDirty = 1;
      continue;
    }

    if (pressed & PSP_CTRL_START)
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      menuSidebarOpen = !menuSidebarOpen;
      menuDirty = 1;
    }
    else if (menuSidebarOpen && (pressed & PSP_CTRL_CIRCLE))
    {
      requestUiSound(soundUiDecline_start, soundUiDecline_end);
      menuSidebarOpen = 0;
      menuDirty = 1;
    }
    else if (menuSidebarOpen && ((pressed & PSP_CTRL_UP) || (pressed & PSP_CTRL_DOWN)))
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      menuSidebarSelected = 0;
      menuDirty = 1;
    }
    else if (!menuSidebarOpen && (pressed & PSP_CTRL_LEFT))
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      menuSelected = (menuSelected + 1) % 2;
      menuDirty = 1;
    }
    else if (!menuSidebarOpen && (pressed & PSP_CTRL_RIGHT))
    {
      requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
      menuSelected = (menuSelected + 1) % 2;
      menuDirty = 1;
    }
    else if (!menuSidebarOpen && (pressed & PSP_CTRL_CIRCLE))
    {
      menuMessage[0] = '\0';
      menuDirty = 1;
    }

    if (pressed & PSP_CTRL_CROSS)
    {
      if (menuSidebarOpen)
      {
        requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
        settingsLoop();
        sceCtrlPeekBufferPositive(&ctrl, 1);
        lastButtons = ctrl.Buttons;
        menuSidebarOpen = 0;
        menuDirty = 1;
      }
      else if (menuSelected == 0)
      {
        int result;
        int disconnected;
        requestUiSound(soundUiConfirm_start, soundUiConfirm_end);

        if (!connectPromptLoop())
        {
          sceCtrlPeekBufferPositive(&ctrl, 1);
          lastButtons = ctrl.Buttons;
          menuDirty = 1;
          continue;
        }
        result = startUsb();
        if (result == 0)
        {
          displayUsbConnectedSmooth();
          disconnected = streamLoop();
          stopUsb();
          sceCtrlPeekBufferPositive(&ctrl, 1);
          lastButtons = ctrl.Buttons;
          if ((appConfig.language ? appConfig.language : defaultLanguage()) == 2)
            snprintf(menuMessage, sizeof(menuMessage), disconnected ? "PC disconnected. Back to menu." : "Stream stopped. Ready to start again.");
          else
            snprintf(menuMessage, sizeof(menuMessage), disconnected ? "PC desconectado. Voltei para o menu." : "Streaming encerrado. Pronto para iniciar de novo.");
          menuDirty = 1;
        }
        else
        {
          if ((appConfig.language ? appConfig.language : defaultLanguage()) == 2)
            snprintf(menuMessage, sizeof(menuMessage), "USB error %d. Check usbhostfs.prx and try again.", result);
          else
            snprintf(menuMessage, sizeof(menuMessage), "Erro USB %d. Confira usbhostfs.prx e tente de novo.", result);
          menuDirty = 1;
        }
      }
      else
      {
        int confirm = 1;
        int confirmDirty = 1;
        requestUiSound(soundUiConfirm_start, soundUiConfirm_end);
        while (running)
        {
          if (confirmDirty)
          {
            displayExitConfirm(confirm);
            confirmDirty = 0;
          }
          sceCtrlPeekBufferPositive(&ctrl, 1);
          pressed = ctrl.Buttons & ~lastButtons;
          lastButtons = ctrl.Buttons;
          if (pressed & PSP_CTRL_CIRCLE)
          {
            requestUiSound(soundUiDecline_start, soundUiDecline_end);
            break;
          }
          if ((pressed & PSP_CTRL_LEFT) || (pressed & PSP_CTRL_RIGHT))
          {
            requestUiSound(soundUiNavigation_start, soundUiNavigation_end);
            confirm = 1 - confirm;
            confirmDirty = 1;
          }
          if (pressed & PSP_CTRL_CROSS)
          {
            requestUiSound(confirm == 0 ? soundUiConfirm_start : soundUiDecline_start,
                           confirm == 0 ? soundUiConfirm_end : soundUiDecline_end);
            if (confirm == 0)
              running = 0;
            break;
          }
          sceKernelDelayThread(16000);
        }
        menuDirty = 1;
      }
    }

    sceKernelDelayThread(16000);
  }

  stopJpeg();
  stopDecodeThread();
  stopUsb();
  stopUiSound();
  displayShutdown();
  sceImposeSetHomePopup(1);
  sceKernelExitGame();
  return 0;
}
