#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <Arduino.h>

// 觸控感測器 — A0 (ADC)，中位數濾波 + 滑動基線
// PWM 背光會在 ADC 上產生雜訊，需要中位數濾波去除尖峰
// 短按 = TAP（切換裝置），長按 = LONG_PRESS（鎖定/解鎖）

class TouchSensor {
public:
    enum Event { NONE, TAP, LONG_PRESS };

    static const uint16_t POLL_INTERVAL_MS = 300;
    static const uint16_t LONG_PRESS_MS = 1500;
    static const uint16_t TAP_COOLDOWN_MS = 500;
    static const uint8_t TOUCH_THRESHOLD = 2;       // 中位數濾波已去雜訊，閾值可保持低
    static const uint8_t CONFIRM_COUNT = 2;          // 連續 2 次超閾值才確認（300ms×2=600ms）

    void begin() {
        resetBaseline();
    }

    // 重設基線（亮度改變後呼叫）
    void resetBaseline() {
        _lastPollAt = 0;
        _touchStartAt = 0;
        _lastEventAt = 0;
        _touching = false;
        _longFired = false;
        _lastRaw = 0;
        _histIdx = 0;
        _histCount = 0;
        _confirmHits = 0;
        // 靜默期：重設後 1 秒不偵測，讓基線穩定
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

        // 靜默期不偵測
        if (now < _silentUntil) return NONE;

        if (now - _lastPollAt < POLL_INTERVAL_MS) {
            if (_touching && !_longFired && (now - _touchStartAt >= LONG_PRESS_MS)) {
                _longFired = true;
                _lastEventAt = now;
                return LONG_PRESS;
            }
            return NONE;
        }
        _lastPollAt = now;

        int raw = readMedian();
        _lastRaw = raw;

        // 滑動最小值基線（用中位數濾波後的值）
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
            // 需要連續 N 次超閾值才確認（過濾雜訊尖峰）
            if (_confirmHits >= CONFIRM_COUNT) {
                if (!_touching) {
                    _touching = true;
                    _touchStartAt = now;
                    _longFired = false;
                }
                if (!_longFired && (now - _touchStartAt >= LONG_PRESS_MS)) {
                    _longFired = true;
                    _lastEventAt = now;
                    return LONG_PRESS;
                }
            }
        } else {
            _confirmHits = 0;
            if (_touching) {
                _touching = false;
                if (!_longFired && (now - _lastEventAt >= TAP_COOLDOWN_MS)) {
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
    uint8_t _confirmHits = 0;
    unsigned long _lastPollAt = 0;
    unsigned long _touchStartAt = 0;
    unsigned long _lastEventAt = 0;
    unsigned long _silentUntil = 0;
    bool _touching = false;
    bool _longFired = false;

    // 中位數濾波：取 5 次讀數的中位數，去除 PWM 雜訊尖峰
    int readMedian() {
        int samples[5];
        for (uint8_t i = 0; i < 5; i++) {
            samples[i] = analogRead(A0);
            delayMicroseconds(300);
        }
        // 簡易排序取中位數
        for (uint8_t i = 0; i < 4; i++) {
            for (uint8_t j = i + 1; j < 5; j++) {
                if (samples[j] < samples[i]) {
                    int tmp = samples[i];
                    samples[i] = samples[j];
                    samples[j] = tmp;
                }
            }
        }
        return samples[2];  // 中位數
    }
};

#endif // TOUCH_SENSOR_H
