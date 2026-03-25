#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <Arduino.h>

// 觸控感測器 — 使用 A0 (ADC) 讀取電容式觸控
// 基線值約 37，觸碰時升至 44-45，差值約 7-8
// 使用自適應基線 + 閾值偵測，支援防抖

class TouchSensor {
public:
    static const uint8_t TOUCH_PIN = A0;
    static const uint8_t TOUCH_THRESHOLD = 5;       // ADC 變化超過此值視為觸碰
    static const uint16_t DEBOUNCE_MS = 300;         // 防抖間隔
    static const uint16_t BASELINE_SAMPLES = 16;     // 基線取樣數
    static const uint16_t BASELINE_UPDATE_MS = 2000; // 基線更新間隔（未觸碰時）

    void begin() {
        // 取初始基線
        long sum = 0;
        for (uint16_t i = 0; i < BASELINE_SAMPLES; i++) {
            sum += analogRead(TOUCH_PIN);
            delay(5);
        }
        _baseline = sum / BASELINE_SAMPLES;
        _lastTouchAt = 0;
        _lastBaselineUpdate = millis();
        _touched = false;
    }

    // 每次 loop 呼叫，回傳 true = 偵測到新的觸碰事件（邊緣觸發）
    bool poll() {
        int raw = analogRead(TOUCH_PIN);
        unsigned long now = millis();
        int diff = raw - _baseline;

        bool currentlyTouched = (diff >= TOUCH_THRESHOLD);

        if (currentlyTouched) {
            if (!_touched && (now - _lastTouchAt >= DEBOUNCE_MS)) {
                _touched = true;
                _lastTouchAt = now;
                return true;  // 新觸碰事件
            }
        } else {
            _touched = false;
            // 未觸碰時緩慢更新基線（適應環境變化）
            if (now - _lastBaselineUpdate >= BASELINE_UPDATE_MS) {
                _baseline = (_baseline * 7 + raw) / 8;  // 指數移動平均
                _lastBaselineUpdate = now;
            }
        }
        return false;
    }

    int getBaseline() const { return _baseline; }
    bool isTouched() const { return _touched; }

private:
    int _baseline = 0;
    unsigned long _lastTouchAt = 0;
    unsigned long _lastBaselineUpdate = 0;
    bool _touched = false;
};

#endif // TOUCH_SENSOR_H
