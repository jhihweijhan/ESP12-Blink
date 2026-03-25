#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <Arduino.h>

// 觸控感測器 — A0 (ADC)，中位數濾波 + 持續時間判斷
// 按住 1~3 秒 = TAP（切換裝置），按住 >3 秒 = LONG_PRESS（鎖定/解鎖）
// 放手後根據持續時間決定事件類型

class TouchSensor {
public:
    enum Event { NONE, TAP, LONG_PRESS };

    static const uint16_t POLL_INTERVAL_MS = 300;
    static const uint16_t TAP_MIN_MS = 1000;          // 最少按住 1 秒才算 TAP
    static const uint16_t LONG_PRESS_MS = 3000;       // 按住 3 秒以上 = 鎖定
    static const uint16_t EVENT_COOLDOWN_MS = 1000;
    static const uint8_t TOUCH_THRESHOLD = 2;
    static const uint8_t CONFIRM_COUNT = 3;            // 連續 3 次超閾值（900ms）

    void begin() {
        resetBaseline();
    }

    void resetBaseline() {
        _lastPollAt = 0;
        _touchStartAt = 0;
        _lastEventAt = 0;
        _touching = false;
        _confirmed = false;
        _longFired = false;
        _lastRaw = 0;
        _histIdx = 0;
        _histCount = 0;
        _confirmHits = 0;
        _silentUntil = millis() + 1000;
        long sum = 0;
        for (uint8_t i = 0; i < 16; i++) {
            sum += readMedian();
            delay(25);
        }
        _baseline = sum / 16;
    }

    Event poll() {
        unsigned long now = millis();
        if (now < _silentUntil) return NONE;

        // 長按偵測（不等 ADC poll 間隔）
        if (_confirmed && !_longFired && (now - _touchStartAt >= LONG_PRESS_MS)) {
            _longFired = true;
            _lastEventAt = now;
            return LONG_PRESS;
        }

        if (now - _lastPollAt < POLL_INTERVAL_MS) return NONE;
        _lastPollAt = now;

        int raw = readMedian();
        _lastRaw = raw;

        // 滑動最小值基線
        _history[_histIdx] = raw;
        _histIdx = (_histIdx + 1) % HIST_SIZE;
        if (_histCount < HIST_SIZE) _histCount++;
        int minVal = 1024;
        for (uint8_t i = 0; i < _histCount; i++) {
            if (_history[i] < minVal) minVal = _history[i];
        }
        _baseline = minVal;

        int diff = raw - _baseline;
        bool aboveThreshold = (diff >= TOUCH_THRESHOLD);

        if (aboveThreshold) {
            _confirmHits++;
            if (_confirmHits >= CONFIRM_COUNT && !_confirmed) {
                _confirmed = true;
                _touchStartAt = now;
                _longFired = false;
            }
        } else {
            // 放手 — 根據持續時間判斷事件
            if (_confirmed) {
                _confirmed = false;
                unsigned long held = now - _touchStartAt;

                if (_longFired) {
                    // 長按已在按住時觸發，放手不再觸發
                } else if (held >= TAP_MIN_MS && (now - _lastEventAt >= EVENT_COOLDOWN_MS)) {
                    // 按住 1~3 秒 = TAP
                    _lastEventAt = now;
                    _confirmHits = 0;
                    _touching = false;
                    return TAP;
                }
            }
            _confirmHits = 0;
            _touching = false;
        }
        return NONE;
    }

    int getBaseline() const { return _baseline; }
    int getLastRaw() const { return _lastRaw; }
    bool isTouching() const { return _confirmed; }

private:
    static const uint8_t HIST_SIZE = 20;
    int _history[HIST_SIZE] = {};
    uint8_t _histIdx = 0;
    uint8_t _histCount = 0;
    int _baseline = 0;
    int _lastRaw = 0;
    uint8_t _confirmHits = 0;
    unsigned long _lastPollAt = 0;
    unsigned long _touchStartAt = 0;
    unsigned long _lastEventAt = 0;
    unsigned long _silentUntil = 0;
    bool _touching = false;
    bool _confirmed = false;
    bool _longFired = false;

    int readMedian() {
        int samples[5];
        for (uint8_t i = 0; i < 5; i++) {
            samples[i] = analogRead(A0);
            delayMicroseconds(300);
        }
        for (uint8_t i = 0; i < 4; i++) {
            for (uint8_t j = i + 1; j < 5; j++) {
                if (samples[j] < samples[i]) {
                    int tmp = samples[i];
                    samples[i] = samples[j];
                    samples[j] = tmp;
                }
            }
        }
        return samples[2];
    }
};

#endif // TOUCH_SENSOR_H
