#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>

#include "display.h"
#include "intraFont.h"

#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define BUF_WIDTH 512
#define PIXEL_SIZE 4

typedef struct
{
  float u;
  float v;
  unsigned int color;
  float x;
  float y;
  float z;
} Vertex;

typedef struct
{
  unsigned int color;
  float x;
  float y;
  float z;
} ColorVertex;

static unsigned int guList[262144] __attribute__((aligned(16)));
static const ScePspFVector3 screenCenter = {240.0f, 136.0f, 0.0f};
static intraFont* fontLarge = 0;
static intraFont* fontSmall = 0;
static int fontInitialized = 0;
static int fontReady = 0;
static int uiLanguage = 1;
static int uiClockMode = 0;
static int fpsOverlayEnabled = 0;
static int fpsOverlayValue = 0;
static int volumeOverlayValue = 65;
static unsigned int volumeOverlayUntil = 0;

extern unsigned char iconStart_start[];
extern unsigned char iconSettings_start[];
extern unsigned char iconExit_start[];
extern unsigned char iconPc_start[];
extern unsigned char iconDpad_start[];
extern unsigned char iconConfirm_start[];
extern unsigned char iconBattery_start[];
extern unsigned char iconClock_start[];

static void setupOrtho(void)
{
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumOrtho(0, SCR_WIDTH, SCR_HEIGHT, 0, -1, 1);
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
}

void displaySetPreferences(int language, int clockMode)
{
  uiLanguage = (language == 2) ? 2 : 1;
  uiClockMode = clockMode ? 1 : 0;
}

void displaySetFpsOverlay(int enabled, int fps)
{
  fpsOverlayEnabled = enabled ? 1 : 0;
  fpsOverlayValue = fps < 0 ? 0 : fps;
}

void displaySetVolumeOverlay(int volume)
{
  if (volume < 0)
    volume = 0;
  if (volume > 100)
    volume = 100;
  volumeOverlayValue = volume;
  volumeOverlayUntil = sceKernelGetSystemTimeLow() + 1400000;
}

static void drawRect(unsigned int topColor, unsigned int bottomColor, float x, float y, float w, float h)
{
  ColorVertex* vertices;

  setupOrtho();
  sceGuDisable(GU_TEXTURE_2D);
  vertices = (ColorVertex*)sceGuGetMemory(4 * sizeof(ColorVertex));

  vertices[0].color = topColor;
  vertices[0].x = x;
  vertices[0].y = y;
  vertices[0].z = 0.0f;

  vertices[1].color = topColor;
  vertices[1].x = x + w;
  vertices[1].y = y;
  vertices[1].z = 0.0f;

  vertices[2].color = bottomColor;
  vertices[2].x = x + w;
  vertices[2].y = y + h;
  vertices[2].z = 0.0f;

  vertices[3].color = bottomColor;
  vertices[3].x = x;
  vertices[3].y = y + h;
  vertices[3].z = 0.0f;

  sceGumDrawArray(GU_TRIANGLE_FAN, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 4, 0, vertices);
  sceGuEnable(GU_TEXTURE_2D);
}

static void drawIcon(void* texture, float x, float y, float size)
{
  Vertex* vertices;

  if (!texture)
    return;

  sceKernelDcacheWritebackRange(texture, 32 * 32 * sizeof(unsigned int));
  setupOrtho();
  sceGuEnable(GU_TEXTURE_2D);
  sceGuEnable(GU_BLEND);
  sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexScale(1.0f / 32.0f, 1.0f / 32.0f);
  sceGuTexImage(0, 32, 32, 32, texture);

  vertices = (Vertex*)sceGuGetMemory(4 * sizeof(Vertex));
  vertices[0].color = 0xffffffff;
  vertices[0].u = 0.0f;
  vertices[0].v = 0.0f;
  vertices[0].x = x;
  vertices[0].y = y;
  vertices[0].z = 0.0f;
  vertices[1].color = 0xffffffff;
  vertices[1].u = 32.0f;
  vertices[1].v = 0.0f;
  vertices[1].x = x + size;
  vertices[1].y = y;
  vertices[1].z = 0.0f;
  vertices[2].color = 0xffffffff;
  vertices[2].u = 32.0f;
  vertices[2].v = 32.0f;
  vertices[2].x = x + size;
  vertices[2].y = y + size;
  vertices[2].z = 0.0f;
  vertices[3].color = 0xffffffff;
  vertices[3].u = 0.0f;
  vertices[3].v = 32.0f;
  vertices[3].x = x;
  vertices[3].y = y + size;
  vertices[3].z = 0.0f;
  sceGumDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 4, 0, vertices);
  sceGuTexScale(1.0f, 1.0f);
}

static void drawUiBase(void)
{
  sceGuClearColor(0xff100701);
  sceGuClearDepth(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

  drawRect(0xff251a10, 0xff180f08, 0, 0, 480, 30);
  drawRect(0xff100701, 0xff100701, 0, 30, 480, 212);
  drawRect(0xff462f18, 0xff462f18, 0, 30, 480, 1);
  drawRect(0xff251a10, 0xff180f08, 0, 242, 480, 30);
  drawRect(0xff462f18, 0xff462f18, 0, 241, 480, 1);
}

static void drawMainBase(void)
{
  sceGuClearColor(0xff322817);
  sceGuClearDepth(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

  drawRect(0xff322817, 0xff241a10, 0, 0, 480, 238);
  drawRect(0xff000000, 0xff000000, 0, 238, 480, 34);
}

static void drawCard(int selected, float x, float y, float w, float h)
{
  unsigned int top = selected ? 0xf0d89a1c : 0xe0251a10;
  unsigned int bottom = selected ? 0xf0885f0d : 0xe019120a;
  unsigned int border = selected ? 0xfff4c066 : 0xff3c2d1c;

  drawRect(border, border, x - 1, y - 1, w + 2, h + 2);
  drawRect(top, bottom, x, y, w, h);
  if (selected)
    drawRect(0x90f4c066, 0x00f4c066, x + 1, y + 1, w - 2, 8);
}

static void drawFont(intraFont* font, float size, unsigned int color, unsigned int shadow, unsigned int align, float x, float y, const char* text)
{
  if (!font || !text)
    return;

  intraFontSetStyle(font, size, color, shadow, align);
  intraFontPrint(font, x, y, text);
}

static void drawFontf(intraFont* font, float size, unsigned int color, unsigned int shadow, unsigned int align, float x, float y, const char* text, int valueA, int valueB)
{
  if (!font || !text)
    return;

  intraFontSetStyle(font, size, color, shadow, align);
  intraFontPrintf(font, x, y, text, valueA, valueB);
}

static void drawHeader(void)
{
  int battery = scePowerGetBatteryLifePercent();
  ScePspDateTime time;
  int english = (uiLanguage == 2);

  drawFont(fontLarge, 0.92f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 16.0f, 23.0f, "PSP");
  drawFont(fontLarge, 0.92f, 0xfff4c066, 0x00000000, INTRAFONT_ALIGN_LEFT, 66.0f, 23.0f, "LINK");
  drawFont(fontSmall, 0.62f, 0x90aa9b8a, 0x00000000, INTRAFONT_ALIGN_LEFT, 140.0f, 23.0f, english ? "play your pc games on psp" : "jogue seus jogos do pc no psp");

  drawIcon(iconBattery_start, 358.0f, 8.0f, 14.0f);
  if (battery >= 0)
    drawFontf(fontSmall, 0.68f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_RIGHT, 405.0f, 20.0f, "%d%%", battery, 0);
  else
    drawFont(fontSmall, 0.68f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_RIGHT, 405.0f, 20.0f, "--%");

  if (sceRtcGetCurrentClockLocalTime(&time) == 0)
  {
    if (uiClockMode)
    {
      int hour = time.hour % 12;
      if (hour == 0)
        hour = 12;
      drawFontf(fontSmall, 0.68f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 20.0f, "%02d:%02d", hour, time.minute);
    }
    else
      drawFontf(fontSmall, 0.68f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 20.0f, "%02d:%02d", time.hour, time.minute);
  }
}

static void drawMainStatus(void)
{
  int battery = scePowerGetBatteryLifePercent();
  ScePspDateTime time;

  drawIcon(iconBattery_start, 354.0f, 13.0f, 13.0f);
  if (battery >= 0)
    drawFontf(fontSmall, 0.66f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_RIGHT, 404.0f, 24.0f, "%d%%", battery, 0);
  else
    drawFont(fontSmall, 0.66f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_RIGHT, 404.0f, 24.0f, "--%");

  if (sceRtcGetCurrentClockLocalTime(&time) == 0)
  {
    if (uiClockMode)
    {
      int hour = time.hour % 12;
      if (hour == 0)
        hour = 12;
      drawFontf(fontSmall, 0.66f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 24.0f, "%02d:%02d", hour, time.minute);
    }
    else
      drawFontf(fontSmall, 0.66f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 24.0f, "%02d:%02d", time.hour, time.minute);
  }
}

static void drawFpsOverlay(void)
{
  if (!fpsOverlayEnabled || !fontReady)
    return;

  drawRect(0xd0101416, 0xd00b0f11, 8, 7, 82, 25);
  drawRect(0xff66d67a, 0xff3ea75a, 8, 7, 3, 25);
  drawFontf(fontSmall, 0.64f, 0xff66ff88, 0x00000000, INTRAFONT_ALIGN_LEFT, 17.0f, 25.0f, "%d FPS", fpsOverlayValue, 0);
}

static void drawVolumeOverlay(void)
{
  float fill;

  if (!fontReady || sceKernelGetSystemTimeLow() > volumeOverlayUntil)
    return;

  fill = 154.0f * volumeOverlayValue / 100.0f;
  drawRect(0xd0101416, 0xd00b0f11, 145, 204, 190, 38);
  drawFont(fontSmall, 0.62f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 162.0f, 224.0f, "VOL");
  drawRect(0xff2a3a3e, 0xff1d2a2d, 162, 229, 154, 7);
  drawRect(0xff66d67a, 0xff3ea75a, 162, 229, fill, 7);
  drawFontf(fontSmall, 0.58f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_RIGHT, 316.0f, 224.0f, "%d%%", volumeOverlayValue, 0);
}

static void drawFooter(const char* message)
{
  drawFont(fontSmall, 0.58f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_LEFT, 18.0f, 257.0f, message ? message : "D-PAD move. X inicia.");
  drawFont(fontSmall, 0.58f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 257.0f, "PSPLINK v1.0");
}

static void drawMainButton(int selected, float x, float y, float w, float h, const char* title)
{
  unsigned int top = selected ? 0xff5a3820 : 0xff22342a;
  unsigned int bottom = selected ? 0xff3d2818 : 0xff17251d;
  unsigned int border = selected ? 0xffffffff : 0xff5f756f;
  unsigned int text = selected ? 0xffffffff : 0xffcfd9dc;

  drawRect(border, border, x - 1, y - 1, w + 2, h + 2);
  drawRect(top, bottom, x, y, w, h);
  drawFont(fontSmall, selected ? 0.82f : 0.74f, text, 0x00000000, INTRAFONT_ALIGN_CENTER, x + w * 0.5f, y + h * 0.58f, title);
}

static void drawMainSidebar(int selected, float amount)
{
  float x = -156.0f + (156.0f * amount);
  int english = (uiLanguage == 2);

  if (amount > 0.05f)
    drawRect(0x902d2216, 0x90241a10, 0, 0, 480, 238);
  drawRect(0xff2d2216, 0xff241a10, x, 0, 156, 238);

  if (!fontReady)
    return;

  drawFont(fontSmall, 0.78f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, x + 18.0f, 35.0f, english ? "MENU" : "MENU");
  if (selected == 0)
    drawRect(0xff5a3820, 0xff3d2818, x + 12.0f, 62, 132, 34);
  drawIcon(iconSettings_start, x + 23.0f, 68.0f, 20.0f);
  drawFont(fontSmall, 0.66f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, x + 52.0f, 86.0f, english ? "SETTINGS" : "CONFIGURACOES");
}

static void drawMainMenuFrame(int selected, float buttonAmount, float sidebarAmount, int sidebarSelected, const char* message)
{
  int english = (uiLanguage == 2);
  float baseW = 112.0f;
  float baseH = 84.0f;
  float growW = 14.0f;
  float growH = 10.0f;
  float connectGrow = (selected == 0) ? buttonAmount : (1.0f - buttonAmount);
  float exitGrow = (selected == 1) ? buttonAmount : (1.0f - buttonAmount);
  float connectW = baseW + growW * connectGrow;
  float exitW = baseW + growW * exitGrow;
  float connectH = baseH + growH * connectGrow;
  float exitH = baseH + growH * exitGrow;
  float connectCenterX = 175.0f;
  float exitCenterX = 305.0f;
  float centerY = 123.0f;

  drawMainBase();
  if (!fontReady)
    return;

  drawMainStatus();
  drawMainButton(selected == 0, connectCenterX - connectW * 0.5f, centerY - connectH * 0.5f, connectW, connectH, english ? "CONNECT" : "CONECTAR");
  drawMainButton(selected == 1, exitCenterX - exitW * 0.5f, centerY - exitH * 0.5f, exitW, exitH, english ? "EXIT" : "SAIR");

  if (sidebarAmount > 0.001f)
    drawMainSidebar(sidebarSelected, sidebarAmount);

  drawRect(0xffffffff, 0xffffffff, 15, 245, 76, 16);
  drawFont(fontSmall, 0.76f, 0xff000000, 0x00000000, INTRAFONT_ALIGN_CENTER, 53.0f, 258.0f, "PsLink");
  drawFont(fontSmall, 0.82f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 102.0f, 258.0f, english ? "menu" : "menu");
  drawVolumeOverlay();
}

void displayInit(void)
{
  pspDebugScreenInit();
  pspDebugScreenClear();

  sceGuInit();
  sceGuStart(GU_DIRECT, guList);
  sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
  sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)(BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE), BUF_WIDTH);
  sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
  sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
  sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
  sceGuDepthRange(0xc350, 0x2710);
  sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuAlphaFunc(GU_GREATER, 0, 0xff);
  sceGuEnable(GU_ALPHA_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuDisable(GU_CULL_FACE);
  sceGuFrontFace(GU_CW);
  sceGuShadeModel(GU_SMOOTH);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
  sceGuTexFunc(GU_TFX_DECAL, GU_TCC_RGB);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexScale(1.0f, 1.0f);
  sceGuAmbientColor(0xffffffff);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuEnable(GU_BLEND);
  sceGuFinish();
  sceGuSync(0, 0);

  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);

  if (intraFontInit() >= 0)
  {
    fontInitialized = 1;
    fontLarge = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_ASCII);
    fontSmall = intraFontLoad("flash0:/font/ltn8.pgf", INTRAFONT_CACHE_ASCII);
    fontReady = (fontLarge && fontSmall);
  }
}

void displayShutdown(void)
{
  if (fontLarge)
    intraFontUnload(fontLarge);
  if (fontSmall)
    intraFontUnload(fontSmall);
  if (fontInitialized)
    intraFontShutdown();

  sceGuTerm();
}

void displayClear(unsigned int color)
{
  sceGuStart(GU_DIRECT, guList);
  sceGuClearColor(color);
  sceGuClearDepth(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
  sceGuFinish();
  sceGuSync(0, 0);
  sceGuSwapBuffers();
}

void displayFrame(unsigned int* pixels, int sourceWidth, int sourceHeight)
{
  float x;

  if (sourceWidth <= 0 || sourceWidth > SCR_WIDTH)
    sourceWidth = SCR_WIDTH;
  if (sourceHeight <= 0 || sourceHeight > SCR_HEIGHT)
    sourceHeight = SCR_HEIGHT;

  sceKernelDcacheWritebackRange(pixels, SCR_WIDTH * sourceHeight * sizeof(unsigned int));

  sceGuStart(GU_DIRECT, guList);
  setupOrtho();
  sceGumTranslate(&screenCenter);
  sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(sourceWidth == SCR_WIDTH && sourceHeight == SCR_HEIGHT ? GU_NEAREST : GU_LINEAR,
                 sourceWidth == SCR_WIDTH && sourceHeight == SCR_HEIGHT ? GU_NEAREST : GU_LINEAR);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexScale(1.0f / 512.0f, 1.0f / 512.0f);
  sceGuTexImage(0, 512, 512, SCR_WIDTH, pixels);

  for (x = 0.0f; x < SCR_WIDTH; x += 32.0f)
  {
    Vertex* vertices = (Vertex*)sceGuGetMemory(4 * sizeof(Vertex));
    float width = ((x + 32.0f) > SCR_WIDTH) ? (SCR_WIDTH - x) : 32.0f;
    float u0 = (x * sourceWidth) / (float)SCR_WIDTH;
    float u1 = ((x + width) * sourceWidth) / (float)SCR_WIDTH;

    vertices[0].color = 0xffffffff;
    vertices[0].u = u0;
    vertices[0].v = 0.0f;
    vertices[0].x = -240.0f + x;
    vertices[0].y = -136.0f;
    vertices[0].z = 0.0f;

    vertices[1].color = 0xffffffff;
    vertices[1].u = u1;
    vertices[1].v = 0.0f;
    vertices[1].x = -240.0f + x + width;
    vertices[1].y = -136.0f;
    vertices[1].z = 0.0f;

    vertices[2].color = 0xffffffff;
    vertices[2].u = u1;
    vertices[2].v = (float)sourceHeight;
    vertices[2].x = -240.0f + x + width;
    vertices[2].y = -136.0f + SCR_HEIGHT;
    vertices[2].z = 0.0f;

    vertices[3].color = 0xffffffff;
    vertices[3].u = u0;
    vertices[3].v = (float)sourceHeight;
    vertices[3].x = -240.0f + x;
    vertices[3].y = -136.0f + SCR_HEIGHT;
    vertices[3].z = 0.0f;

    sceGumDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 4, 0, vertices);
  }

  sceGuTexScale(1.0f, 1.0f);
  drawFpsOverlay();
  drawVolumeOverlay();
  sceGuFinish();
  sceGuSync(0, 0);
  sceGuSwapBuffers();
}

void displayText(const char* a, const char* b, const char* c)
{
  pspDebugScreenClear();
  pspDebugScreenSetXY(2, 5);
  pspDebugScreenPrintf("%s\n\n%s\n%s", a, b, c);
}

void displayStatus(const char* title, const char* status, const char* hint)
{
  displayClear(0xff101010);
  pspDebugScreenSetTextColor(0xff66c0f4);
  pspDebugScreenSetXY(4, 4);
  pspDebugScreenPrintf("%s", title);
  pspDebugScreenSetTextColor(0xffc7d5e0);
  pspDebugScreenSetXY(4, 8);
  pspDebugScreenPrintf("%s", status);
  pspDebugScreenSetTextColor(0xff8f98a0);
  pspDebugScreenSetXY(4, 13);
  pspDebugScreenPrintf("%s", hint);
  pspDebugScreenSetTextColor(0xffffffff);
}

void displayMainMenu(int selected, int connected, const char* message)
{
  (void)connected;
  displayMainMenuEx(selected, 0, 0, message);
}

void displayMainMenuEx(int selected, int sidebarOpen, int sidebarSelected, const char* message)
{
  int pass;
  static int lastSelected = -1;
  static int lastSidebarOpen = 0;
  int frames = (lastSelected >= 0 && (lastSelected != selected || lastSidebarOpen != sidebarOpen)) ? 8 : 2;
  int selectionChanged = (lastSelected >= 0 && lastSelected != selected);
  int sidebarChanged = (lastSidebarOpen != sidebarOpen);

  if (selected < 0)
    selected = 0;
  if (selected > 1)
    selected = 1;

  for (pass = 0; pass < frames; pass++)
  {
    float progress = (frames <= 2) ? 1.0f : (pass + 1) / (float)frames;
    float buttonAmount = selectionChanged ? progress : 1.0f;
    float sidebarAmount = sidebarOpen ? (sidebarChanged ? progress : 1.0f) : (sidebarChanged ? 1.0f - progress : 0.0f);
    sceGuStart(GU_DIRECT, guList);
    drawMainMenuFrame(selected, buttonAmount, sidebarAmount, sidebarSelected, message);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
  }

  lastSelected = selected;
  lastSidebarOpen = sidebarOpen;
  if (!fontReady)
    displayStatus("PSPLINK", "intraFont nao iniciou", "Verifique flash0:/font/ltn0.pgf");
}

void displayUsbWaiting(int connected, float buttonAmount)
{
  int pass;
  int english = (uiLanguage == 2);
  (void)connected;
  (void)buttonAmount;

  for (pass = 0; pass < 2; pass++)
  {
    sceGuStart(GU_DIRECT, guList);
    sceGuClearColor(0xff322817);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    drawRect(0xff322817, 0xff241a10, 0, 0, 480, 272);
    drawRect(0xff000000, 0xff000000, 0, 238, 480, 34);
    if (fontReady)
    {
      drawFont(fontLarge, 1.22f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_CENTER, 240.0f, 92.0f, "PsLink");
      drawFont(fontSmall, 0.82f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_CENTER, 240.0f, 137.0f, english ? "Waiting for PC" : "Esperando PC");
      drawFont(fontSmall, 0.62f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_CENTER, 240.0f, 181.0f, english ? "Keep PSPLink open on the computer." : "Mantenha o PSPLink aberto no computador.");

      drawRect(0xffffffff, 0xffffffff, 15, 245, 76, 16);
      drawFont(fontSmall, 0.76f, 0xff000000, 0x00000000, INTRAFONT_ALIGN_CENTER, 53.0f, 258.0f, "PsLink");
      drawFont(fontSmall, 0.82f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 102.0f, 258.0f, english ? "connect" : "conectar");
      drawVolumeOverlay();
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuSwapBuffers();
  }

  if (!fontReady)
    displayStatus("PSPLINK USB", english ? "Waiting for PC connection..." : "Aguardando conexao do PC...", "");
}

void displayExitConfirm(int selected)
{
  int pass;
  int english = (uiLanguage == 2);

  for (pass = 0; pass < 2; pass++)
  {
    sceGuStart(GU_DIRECT, guList);
    sceGuClearColor(0xff322817);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    drawRect(0xff322817, 0xff241a10, 0, 0, 480, 272);
    if (fontReady)
    {
      drawFont(fontLarge, 0.90f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 18.0f, 30.0f, english ? "EXIT" : "SAIR");
      drawFont(fontSmall, 0.62f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 29.0f, english ? "O back" : "O voltar");
      drawFont(fontLarge, 1.18f, 0xff66d67a, 0x00000000, INTRAFONT_ALIGN_CENTER, 240.0f, 99.0f, "?");
      drawFont(fontSmall, 0.74f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_CENTER, 240.0f, 137.0f, english ? "Do you really want to exit PSPLINK?" : "Deseja realmente sair do PSPLINK?");
      drawCard(selected == 0, 112, 170, 122, 42);
      drawCard(selected == 1, 246, 170, 122, 42);
      drawFont(fontSmall, 0.74f, selected == 0 ? 0xffffffff : 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_CENTER, 173.0f, 198.0f, english ? "YES" : "SIM");
      drawFont(fontSmall, 0.74f, selected == 1 ? 0xffffffff : 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_CENTER, 307.0f, 198.0f, english ? "NO" : "NAO");
      drawFont(fontSmall, 0.58f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_CENTER, 240.0f, 238.0f, english ? "X confirm" : "X confirmar");
      drawVolumeOverlay();
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuSwapBuffers();
  }
}

static void drawOverlayBase(void)
{
  drawRect(0xff322817, 0xff241a10, 0, 0, 480, 272);
  drawRect(0xff2d2216, 0xff241a10, 0, 0, 480, 34);
  drawRect(0x803c2d1c, 0x803c2d1c, 0, 34, 480, 1);
}

static void drawSettingsRow(int active, float y, const char* label, const char* value)
{
  if (active)
    drawRect(0x705a3820, 0x503d2818, 168, y - 21.0f, 286, 36);
  drawFont(fontSmall, 0.74f, 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_LEFT, 181.0f, y + 4.0f, label);
  if (value)
    drawFont(fontSmall, 0.72f, active ? 0xffffffff : 0xffcfd9dc, 0x00000000, INTRAFONT_ALIGN_RIGHT, 442.0f, y + 4.0f, value);
}

void displaySettingsMenu(int tab, int row, int language, int clockMode, int fpsOverlay, int audioVolume, int controlsEnabled)
{
  int pass;
  int english = (language == 2);
  const char* langValue = english ? "English" : "Portugues";
  const char* clockValue = clockMode ? "12 horas" : "24 horas";
  const char* fpsValue = fpsOverlay ? (english ? "On" : "Ligado") : (english ? "Off" : "Desligado");
  const char* controlValue = controlsEnabled ? (english ? "On" : "Ligado") : (english ? "Off" : "Desligado");
  char volumeValue[16];
  snprintf(volumeValue, sizeof(volumeValue), "%d%%", audioVolume);

  for (pass = 0; pass < 2; pass++)
  {
    sceGuStart(GU_DIRECT, guList);
    sceGuClearColor(0xff322817);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    drawRect(0xff322817, 0xff241a10, 0, 0, 480, 272);
    drawRect(0xff2d2216, 0xff241a10, 0, 0, 154, 272);
    if (fontReady)
    {
      drawFont(fontLarge, 0.78f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 18.0f, 38.0f, english ? "SETTINGS" : "CONFIG");
      if (tab == 0)
        drawRect(0xff5a3820, 0xff3d2818, 10, 62, 134, 34);
      drawFont(fontSmall, 0.70f, tab == 0 ? 0xffffffff : 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_LEFT, 28.0f, 84.0f, english ? "System" : "Sistema");
      if (tab == 1)
        drawRect(0xff5a3820, 0xff3d2818, 10, 101, 134, 34);
      drawFont(fontSmall, 0.70f, tab == 1 ? 0xffffffff : 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_LEFT, 28.0f, 123.0f, english ? "Sound" : "Sons");
      if (tab == 2)
        drawRect(0xff5a3820, 0xff3d2818, 10, 140, 134, 34);
      drawFont(fontSmall, 0.70f, tab == 2 ? 0xffffffff : 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_LEFT, 28.0f, 162.0f, english ? "Controls" : "Controle");
      drawFont(fontSmall, 0.54f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_LEFT, 20.0f, 248.0f, "L/R");

      drawFont(fontLarge, 0.86f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 178.0f, 50.0f,
               tab == 0 ? (english ? "System" : "Sistema") : tab == 1 ? (english ? "Sound" : "Sons") : (english ? "Controls" : "Controle"));
      if (tab == 0)
      {
        drawSettingsRow(row == 0, 92.0f, english ? "Language" : "Linguagem", langValue);
        drawSettingsRow(row == 1, 132.0f, english ? "Clock" : "Horario", clockValue);
        drawSettingsRow(row == 2, 172.0f, english ? "FPS overlay" : "Overlay FPS", fpsValue);
      }
      else if (tab == 1)
      {
        drawSettingsRow(row == 0, 92.0f, english ? "Volume" : "Volume", volumeValue);
      }
      else
      {
        drawSettingsRow(row == 0, 92.0f, english ? "Controller" : "Controle", controlValue);
      }
      drawVolumeOverlay();
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuSwapBuffers();
  }
}

static void drawOverlayItem(int selected, float y, void* icon, const char* title, const char* subtitle)
{
  if (selected)
    drawRect(0xff5a3820, 0xff3d2818, 82, y - 21.0f, 316, 42);
  else
    drawRect(0xe022342a, 0xe017251d, 82, y - 21.0f, 316, 42);

  drawIcon(icon, 98.0f, y - 12.0f, 24.0f);
  drawFont(fontSmall, 0.76f, selected ? 0xffffffff : 0xffdfdcd8, 0x00000000, INTRAFONT_ALIGN_LEFT, 134.0f, y + 5.0f, title);
  if (subtitle)
    drawFont(fontSmall, 0.52f, selected ? 0xffdfdcd8 : 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_RIGHT, 384.0f, y + 5.0f, subtitle);
}

void displayStreamMenu(int selected)
{
  int pass;
  int english = (uiLanguage == 2);

  for (pass = 0; pass < 2; pass++)
  {
    sceGuStart(GU_DIRECT, guList);
    setupOrtho();
    drawOverlayBase();
    if (fontReady)
    {
      drawFont(fontLarge, 0.88f, 0xffffffff, 0x00000000, INTRAFONT_ALIGN_LEFT, 18.0f, 25.0f, "PSPLINK");
      drawFont(fontSmall, 0.62f, 0xffaa9b8a, 0x00000000, INTRAFONT_ALIGN_RIGHT, 462.0f, 25.0f, english ? "stream menu" : "menu da transmissao");
      drawOverlayItem(selected == 0, 91.0f, iconStart_start, english ? "RESUME" : "RESUMIR", NULL);
      drawOverlayItem(selected == 1, 140.0f, iconSettings_start, english ? "SETTINGS" : "CONFIGURACOES", NULL);
      drawOverlayItem(selected == 2, 189.0f, iconExit_start, english ? "STOP STREAM" : "SAIR DA TRANSMISSAO", NULL);
      drawVolumeOverlay();
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuSwapBuffers();
  }
}
