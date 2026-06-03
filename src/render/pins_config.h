#pragma once
// LilyGo T-Display-S3-Long (AXS15231B over QSPI). From official pins_config.h.
#define TFT_QSPI_CS   12
#define TFT_QSPI_SCK  17
#define TFT_QSPI_D0   13
#define TFT_QSPI_D1   18
#define TFT_QSPI_D2   21
#define TFT_QSPI_D3   14
#define TFT_QSPI_RST  16
#define TFT_BL         1

#define PANEL_NATIVE_W 180
#define PANEL_NATIVE_H 640
#define SCREEN_W       640   // landscape (rotation applied)
#define SCREEN_H       180

#define PIN_BUTTON_BOOT 0
