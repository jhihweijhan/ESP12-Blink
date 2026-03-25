#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <Arduino.h>

// 觸控感測器 — A0 (ADC)，滑動最小值基線，閾值偵測
// 短按 = TAP（切換裝置），長按 = LONG_PRESS（鎖定/解鎖）

class TouchSensor {
public:
    enum Event { NONE, TAP, LONG_PRESS };

    static const uint16_t POLL_INTERVAL_MS = 300;
    static const uint16_t LONG_PRESS_MS = 1500;   // 長按 1.5 秒
    static const uint16_t TAP_COOLDOWN_MS = 500;

    void begin() {
        _lastPollAt = 0;
        _touchStartAt = 0;
        _lastEventAt = 0;
        _touching = false;
        _longFired = false;
        _lastRaw = 0;
        _histIdx = 0;
        _histCount = 0;
        long sum = 0;
        for (uint8_t i = 0; i < 8; i++) {
            sum += analogRead(A0);
            delay(20);
        }
        _baseline = sum / 8;
    }

    // 回傳事件：NONE / TAP / LONG_PRESS
    Event poll() {
        unsigned long now = millis();
        if (now - _lastPollAt < POLL_INTERVAL_MS) {
            // 即使不讀 ADC，也要檢查長按計時
            if (_touching && !_longFired && (now - _touchStartAt >= LONG_PRESS_MS)) {
                _longFired = true;
                _lastEventAt = now;
                return LONG_PRESS;
            }
            return NONE;
        }
        _lastPollAt = now;

        // 多次取樣平均
        long sum = 0;
        for (uint8_t i = 0; i < 8; i++) {
            sum += analogRead(A0);
            delayMicroseconds(200);
        }
        int raw = sum / 8;
        _lastRaw = raw;
        int diff = raw - _baseline;

        // 滑動最小值基線
        _history[_histIdx] = raw;
        _histIdx = (_histIdx + 1) % HIST_SIZE;
        if (_histCount < HIST_SIZE) _histCount++;
        int minVal = 1024;
        for (uint8_t i = 0; i < _histCount; i++) {
            if (_history[i] < minVal) minVal = _history[i];
        }
        _baseline = minVal;

        bool isTouching = (diff >= _threshold);

        if (isTouching) {
            if (!_touching) {
                // 觸碰開始
                _touching = true;
                _touchStartAt = now;
                _longFired = false;
            }
            // 檢查長按
            if (!_longFired && (now - _touchStartAt >= LONG_PRESS_MS)) {
                _longFired = true;
                _lastEventAt = now;
                return LONG_PRESS;
            }
        } else {
            if (_touching) {
                // 觸碰結束
                _touching = false;
                if (!_longFired && (now - _lastEventAt >= TAP_COOLDOWN_MS)) {
                    // 短按（沒有觸發長按）
                    _lastEventAt = now;
                    return TAP;
                }
            }
        }
        return NONE;
    }

    int getBaseline() const { return _baseline; }
    int getLastRaw() const { return _lastRaw; }
    bool isTouching() const { return _touching; }

private:
    static const uint8_t HIST_SIZE = 20;
    int _history[HIST_SIZE] = {};
    uint8_t _histIdx = 0;
    uint8_t _histCount = 0;
    int _baseline = 0;
    int _lastRaw = 0;
    int _threshold = 2;
    unsigned long _lastPollAt = 0;
    unsigned long _touchStartAt = 0;
    unsigned long _lastEventAt = 0;
    bool _touching = false;
    bool _longFired = false;
};

#endif // TOUCH_SENSOR_H
