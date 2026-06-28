TARGET = psplinkusb

ICON_OBJS = assets/icons/start.o assets/icons/settings.o assets/icons/exit.o assets/icons/pc.o assets/icons/dpad.o assets/icons/confirm.o assets/icons/battery.o assets/icons/clock.o
SOUND_OBJS = assets/sounds/ui_confirm.o assets/sounds/ui_decline.o assets/sounds/ui_navigation.o
OBJS = main.o display.o $(ICON_OBJS) $(SOUND_OBJS) ../../../psp/source/library/intraFont.o ../../../psp/source/library/libccc.o ../../../psp/source/usbhostfs/USBHostFS.o

INCDIR = ../../../psp/source/usbhostfs ../../../psp/source/library
LIBDIR = ../../../psp/source/libjpeg
CFLAGS = -O2 -G0 -Wall -fcommon -D_PSP_FW_VERSION=371
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBS = -lpspaudio -lpspusb -lpspgu -lpspgum -lpspjpeg -lpsputility -lpsppower -lpsprtc -lpspdebug -lm -ljpeg

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPLink USB
PSP_FW_VERSION = 371
BUILD_PRX = 1

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

.PHONY: prx package package-clean

prx:
	$(MAKE) -C ../../../psp/source/usbhostfs

package: prx EBOOT.PBP
	rm -rf build/PSPLinkUSB
	mkdir -p build/PSPLinkUSB
	cp EBOOT.PBP build/PSPLinkUSB/
	cp ../../../psp/source/usbhostfs/usbhostfs.prx build/PSPLinkUSB/
	@echo "Package ready: build/PSPLinkUSB"

package-clean:
	rm -rf build/PSPLinkUSB

assets/icons/start.o: assets/icons/start.raw
	bin2o -i assets/icons/start.raw assets/icons/start.o iconStart

assets/icons/settings.o: assets/icons/settings.raw
	bin2o -i assets/icons/settings.raw assets/icons/settings.o iconSettings

assets/icons/exit.o: assets/icons/exit.raw
	bin2o -i assets/icons/exit.raw assets/icons/exit.o iconExit

assets/icons/pc.o: assets/icons/pc.raw
	bin2o -i assets/icons/pc.raw assets/icons/pc.o iconPc

assets/icons/dpad.o: assets/icons/dpad.raw
	bin2o -i assets/icons/dpad.raw assets/icons/dpad.o iconDpad

assets/icons/confirm.o: assets/icons/confirm.raw
	bin2o -i assets/icons/confirm.raw assets/icons/confirm.o iconConfirm

assets/icons/battery.o: assets/icons/battery.raw
	bin2o -i assets/icons/battery.raw assets/icons/battery.o iconBattery

assets/icons/clock.o: assets/icons/clock.raw
	bin2o -i assets/icons/clock.raw assets/icons/clock.o iconClock

assets/sounds/ui_confirm.o: assets/sounds/ui_confirm.wav
	bin2o -i assets/sounds/ui_confirm.wav assets/sounds/ui_confirm.o soundUiConfirm

assets/sounds/ui_decline.o: assets/sounds/ui_decline.wav
	bin2o -i assets/sounds/ui_decline.wav assets/sounds/ui_decline.o soundUiDecline

assets/sounds/ui_navigation.o: assets/sounds/ui_navigation.wav
	bin2o -i assets/sounds/ui_navigation.wav assets/sounds/ui_navigation.o soundUiNavigation
