#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <Arduino.h>
#include "tft_driver.h"

class UIComponents {
public:
    UIComponents(TFTDriver& tft) : _tft(tft) {}

    // 繪製設備標題列
    void drawDeviceHeader(const char* name, bool isOnline = true) {
        uint16_t bgColor = isOnline ? 0x1082 : COLOR_RED;  // 深藍或紅色
        _tft.fillRect(0, 0, TFT_WIDTH, 28, bgColor);
        _tft.drawStringCentered(6, name, COLOR_WHITE, bgColor, 2);
    }

private:
    TFTDriver& _tft;
};

#endif
