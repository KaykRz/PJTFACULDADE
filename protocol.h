#ifndef PROTOCOL_H
#define PROTOCOL_H

#define COM_HEADER_MAGIC 0xAFFE0600

#define COM_FLAGS_CONTAINS_IMAGE_DATA       0x00000001
#define COM_FLAGS_CONTAINS_AUDIO_DATA       0x00000002
#define COM_FLAGS_CONTAINS_SETTINGS_DATA    0x00000004

#define COM_FLAGS_IMAGE_IS_JPEG             0x00000010
#define COM_FLAGS_IMAGE_IS_PNG              0x00000020
#define COM_FLAGS_IMAGE_IS_UNCOMPRESSED     0x00000040
#define COM_FLAGS_PLAS_432X240              0x00000100
#define COM_FLAGS_PLAS_400X224              0x00000200
#define COM_FLAGS_PLAS_368X208              0x00000400

#define COM_FLAGS_AUDIO_22050_HZ            0x00002000
#define COM_FLAGS_AUDIO_44100_HZ            0x00004000
#define COM_FLAGS_AUDIO_CHUNK_2240          0x00010000
#define COM_FLAGS_AUDIO_CHUNK_2688          0x00020000
#define COM_FLAGS_AUDIO_CHUNK_1120          0x00040000
#define COM_FLAGS_AUDIO_HAS_SEQUENCE        0x00080000
#define COM_FLAGS_AUDIO_CHUNK_1024_MONO     0x00200000
#define COM_FLAGS_AUDIO_CHUNK_2048_MONO     0x00400000

#define COM_MAX_PC_CONTROLS 20
#define COM_MAX_PC_DEVICES 10
#define COM_MAX_PC_PRESETS 20

typedef struct __attribute__((__packed__))
{
  unsigned int deviceIndex;
  unsigned int deviceUseSideShow;
  unsigned int screenMode;
  unsigned int screenRotation;
  unsigned int screenViewport;
  unsigned int quality;
  unsigned int screenNoCompression;
  unsigned int updateInterval;
  unsigned int soundEnabled;
  unsigned int soundSampleRate;
  unsigned int controlIndex;
  unsigned int controlCount;
  unsigned int displayDeviceIndex;
  unsigned int displayDeviceCount;
  unsigned int displayEnable;
  unsigned int displayAutoselect;
  unsigned int presetIndex;
  unsigned int presetCount;
  unsigned int presetDoSet;
} ComSettings;

typedef struct __attribute__((__packed__))
{
  unsigned int magic;
  unsigned int flags;
  unsigned int imageSize;
  unsigned int settingsSize;
} ComFrameHeader;

typedef struct __attribute__((__packed__))
{
  unsigned int magic;
  unsigned int flags;
  unsigned int buttons;
  unsigned char analogX;
  unsigned char analogY;
} ComResponseHeader;

typedef struct __attribute__((__packed__))
{
  ComResponseHeader response;
  ComSettings settings;
} ComSettingsResponse;

#define PSPLINK_RESPONSE_RIGHT_ANALOG 0x10000000
#define PSPLINK_RESPONSE_L2           0x20000000
#define PSPLINK_RESPONSE_R2           0x40000000

#endif
