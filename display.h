#ifndef DISPLAY_H
#define DISPLAY_H

void displayInit(void);
void displayShutdown(void);
void displaySetPreferences(int language, int clockMode);
void displaySetFpsOverlay(int enabled, int fps);
void displaySetVolumeOverlay(int volume);
void displayClear(unsigned int color);
void displayFrame(unsigned int* pixels, int sourceWidth, int sourceHeight);
void displayText(const char* a, const char* b, const char* c);
void displayStatus(const char* title, const char* status, const char* hint);
void displayMainMenu(int selected, int connected, const char* message);
void displayMainMenuEx(int selected, int sidebarOpen, int sidebarSelected, const char* message);
void displayUsbWaiting(int connected, float buttonAmount);
void displayExitConfirm(int selected);
void displaySettingsMenu(int tab, int row, int language, int clockMode, int fpsOverlay, int audioVolume, int controlsEnabled);
void displayStreamMenu(int selected);

#endif
