#ifndef MQTT_TRANSPORT_H
#define MQTT_TRANSPORT_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espMqttClientAsync.h>

#include "connection_policy.h"
#include "device_store.h"
#include "metrics_parser_v2.h"
#include "monitor_config.h"

class MQTTTransport {
public:
    typedef void (*MetricsCallback)(const char* hostname);

    MQTTTransport() {}

    MetricsCallback onMetricsReceived = nullptr;

    void begin(MonitorConfigManager& configMgr, DeviceStore& store) {
        _configMgr = &configMgr;
        _store = &store;

        _client.onConnect([this](bool sessionPresent) {
            _state = MqttConnectionState::CONNECTED;
            _consecutiveFailures = 0;
            unsigned long now = millis();
            _lastConnectedAt = now;
            _lastMessageAt = now;
            Serial.println("MQTT connected");
            subscribeConfiguredTopics();
        });

        _client.onDisconnect([this](espMqttClientTypes::DisconnectReason reason) {
            Serial.printf("MQTT disconnected: %u\n", static_cast<uint8_t>(reason));
            _state = MqttConnectionState::DISCONNECTED;
            scheduleReconnect();
        });

        _client.onMessage([this](
            const espMqttClientTypes::MessageProperties& properties,
            const char* topic,
            const uint8_t* payload,
            size_t len, size_t index, size_t total) {
            if (total > MQTT_MAX_PAYLOAD_BYTES) {
                return;
            }
            if (index == 0 && len == total) {
                handleMessage(const_cast<char*>(topic),
                              const_cast<uint8_t*>(payload), static_cast<unsigned int>(len));
            } else {
                if (index == 0) {
                    _msgBufLen = 0;
                }
                if (_msgBufLen + len <= MQTT_MAX_PAYLOAD_BYTES) {
                    memcpy(_msgBuf + _msgBufLen, payload, len);
                    _msgBufLen += len;
                }
                if (_msgBufLen == total) {
                    handleMessage(const_cast<char*>(topic), _msgBuf,
                                  static_cast<unsigned int>(_msgBufLen));
                    _msgBufLen = 0;
                }
            }
        });
    }

    void connect() {
        if (!_configMgr || strlen(_configMgr->config.mqttServer) == 0) {
            Serial.println("MQTT server not configured");
            return;
        }

        if (WiFi.status() != WL_CONNECTED) {
            // WiFi down — retry in 1s without incrementing failure count.
            // Once WiFi comes back, we reconnect immediately.
            _nextReconnectAt = millis() + MQTT_RECONNECT_BASE_MS;
            _needsReconnect = true;
            return;
        }

        uint32_t maxBlock = ESP.getMaxFreeBlockSize();
        if (maxBlock < MQTT_MIN_HEAP_FOR_CONNECT) {
            Serial.printf("MQTT connect deferred: max_block=%u < %u\n",
                          maxBlock, (uint32_t)MQTT_MIN_HEAP_FOR_CONNECT);
            scheduleReconnect();
            return;
        }

        _client.setServer(_configMgr->config.mqttServer, _configMgr->config.mqttPort);
        // Disable MQTT-level keepalive. espMqttClientAsync's PINGREQ
        // mechanism is unreliable on ESP8266 — the broker disconnects
        // after 1.5× keepalive when no PINGREQ arrives.
        // We use our own silence check (MQTT_CONNECTED_SILENCE_TIMEOUT_MS)
        // to detect zombie connections instead.
        _client.setKeepAlive(0);

        snprintf(_clientId, sizeof(_clientId), "ESP12-v2-%04x", (unsigned)random(0xffff));
        _client.setClientId(_clientId);

        if (strlen(_configMgr->config.mqttUser) > 0) {
            _client.setCredentials(_configMgr->config.mqttUser,
                                   _configMgr->config.mqttPass);
        }

        _state = MqttConnectionState::RECONNECTING;
        _reconnectingStartMs = millis();
        bool ok = _client.connect();
        if (!ok) {
            Serial.println("MQTT connect() returned false");
            _state = MqttConnectionState::DISCONNECTED;
            scheduleReconnect();
            return;
        }
        Serial.printf("Connecting MQTT: %s:%d\n",
                       _configMgr->config.mqttServer,
                       _configMgr->config.mqttPort);
    }

    void loop() {
        if (!_configMgr || !_store || strlen(_configMgr->config.mqttServer) == 0) {
            return;
        }

        unsigned long now = millis();

        // Drive espMqttClientAsync state machine. Critical: after
        // disconnect(true), the client needs multiple loop() calls
        // to progress through disconnectingTcp1 -> disconnectingTcp2
        // -> disconnected. Without this, connect() returns false.
        _client.loop();

        // --- WiFi guard ---
        // If WiFi drops, transition to DISCONNECTED immediately so we
        // can reconnect as soon as WiFi comes back. Don't call
        // _client.disconnect(true) — let espMqttClient detect the
        // TCP loss on its own to avoid PCB issues.
        if (_state == MqttConnectionState::CONNECTED && WiFi.status() != WL_CONNECTED) {
            Serial.println("MQTT: WiFi lost, marking disconnected");
            _state = MqttConnectionState::DISCONNECTED;
            _consecutiveFailures = 0;  // Reset so we reconnect in 1s when WiFi returns
            _nextReconnectAt = millis() + MQTT_RECONNECT_BASE_MS;
            _needsReconnect = true;
        }

        // --- Reconnect scheduler ---
        if (_needsReconnect && _state == MqttConnectionState::DISCONNECTED) {
            if ((long)(now - _nextReconnectAt) >= 0) {
                // Wait for espMqttClient to fully reach disconnected state.
                // connect() requires _client.disconnected() == true.
                if (!_client.disconnected()) {
                    return;  // Try again next loop — _client.loop() above will advance state
                }
                _needsReconnect = false;
                connect();
            }
        }

        // --- RECONNECTING timeout ---
        if (_state == MqttConnectionState::RECONNECTING &&
            (long)(now - _reconnectingStartMs) > (long)MQTT_RECONNECTING_TIMEOUT_MS) {
            Serial.println("MQTT reconnecting timeout");
            _client.disconnect(true);
            _state = MqttConnectionState::DISCONNECTED;
            scheduleReconnect();
        }

        // --- Silence check: zombie connection detection ---
        // If MQTT shows CONNECTED but no messages arrive for too long,
        // the connection may be dead. Force reconnect.
        if (_state == MqttConnectionState::CONNECTED &&
            _lastMessageAt > 0 &&
            (long)(now - _lastMessageAt) > (long)MQTT_CONNECTED_SILENCE_TIMEOUT_MS) {
            Serial.printf("MQTT: no messages for %lus, forcing reconnect\n",
                          (unsigned long)((now - _lastMessageAt) / 1000));
            _client.disconnect(true);
            _state = MqttConnectionState::DISCONNECTED;
            scheduleReconnect();
        }

        // --- Device offline check ---
        if (now - _lastOfflineCheckAt >= 1000UL) {
            _lastOfflineCheckAt = now;
            _store->markOfflineExpired(now, getOfflineTimeoutMs());
        }
    }

    MqttConnectionState getConnectionState() const { return _state; }
    bool isConnected() const { return _state == MqttConnectionState::CONNECTED; }

    bool isTopicInAllowlist(const char* topic) const {
        if (!_configMgr || !topic) {
            return false;
        }

        for (uint8_t i = 0; i < _configMgr->config.subscribedTopicCount; i++) {
            if (strcmp(_configMgr->config.subscribedTopics[i], topic) == 0) {
                return true;
            }
        }
        return false;
    }

    bool hasTopicAllowlist() const {
        return _configMgr && _configMgr->config.subscribedTopicCount > 0;
    }

    void handleMessage(char* topic, uint8_t* payload, unsigned int length) {
        if (!_configMgr || !_store) {
            return;
        }

        if (!isValidMqttPayloadLength(length)) {
            Serial.printf("MQTT payload rejected: %u bytes\n", length);
            return;
        }

        bool allowlistMode = hasTopicAllowlist();
        if (allowlistMode && !isTopicInAllowlist(topic)) {
            return;
        }
        if (!allowlistMode && !isValidSenderMetricsTopic(topic)) {
            return;
        }

        char hostname[32];
        MetricsFrameV2 frame;
        if (!parseMetricsV2Payload(topic, payload, length, hostname, sizeof(hostname), frame)) {
            Serial.printf("Drop invalid metrics v2 payload on topic: %s\n", topic ? topic : "<null>");
            return;
        }

        DeviceConfig* cfg = _configMgr->getOrCreateDevice(hostname);
        if (cfg && !cfg->enabled) {
            bool canAutoEnable = shouldAutoEnableDeviceOnSubscribedTopic(_configMgr->config.subscribedTopicCount) &&
                                 isTopicInAllowlist(topic);
            if (!canAutoEnable && !allowlistMode) {
                canAutoEnable = true;
            }
            if (canAutoEnable) {
                cfg->enabled = true;
                _configMgr->markDirty();
            } else {
                return;
            }
        }

        unsigned long now = millis();
        if (!_store->updateFrame(hostname, frame, now)) {
            Serial.println("Drop metrics: device store is full");
            return;
        }

        _lastMessageAt = now;
        _rxMessageCount++;

        if (now - _lastRxLogAt >= MQTT_RX_LOG_INTERVAL_MS) {
            Serial.printf("MQTT rx v2: %u msgs / %ums, last=%s\n",
                          _rxMessageCount,
                          (unsigned int)MQTT_RX_LOG_INTERVAL_MS,
                          hostname);
            _rxMessageCount = 0;
            _lastRxLogAt = now;
        }

        if (onMetricsReceived) {
            onMetricsReceived(hostname);
        }
    }

private:
    espMqttClientAsync _client;
    MqttConnectionState _state = MqttConnectionState::DISCONNECTED;
    bool _needsReconnect = false;
    unsigned long _nextReconnectAt = 0;
    unsigned long _disconnectedAt = 0;
    MonitorConfigManager* _configMgr = nullptr;
    DeviceStore* _store = nullptr;
    char _clientId[20];
    uint8_t _msgBuf[MQTT_MAX_PAYLOAD_BYTES];
    size_t _msgBufLen = 0;
    uint8_t _consecutiveFailures = 0;
    unsigned long _lastRxLogAt = 0;
    uint16_t _rxMessageCount = 0;
    unsigned long _lastConnectedAt = 0;
    unsigned long _lastMessageAt = 0;
    unsigned long _lastOfflineCheckAt = 0;
    unsigned long _reconnectingStartMs = 0;

    unsigned long getOfflineTimeoutMs() const {
        if (!_configMgr) {
            return 30000;
        }

        uint16_t sec = _configMgr->config.offlineTimeoutSec;
        if (sec < MIN_OFFLINE_TIMEOUT_SEC) {
            sec = MIN_OFFLINE_TIMEOUT_SEC;
        }
        if (sec > MAX_OFFLINE_TIMEOUT_SEC) {
            sec = MAX_OFFLINE_TIMEOUT_SEC;
        }
        return (unsigned long)sec * 1000UL;
    }

    void scheduleReconnect() {
        // Prevent double-counting: RECONNECTING timeout and onDisconnect
        // callback can both fire for the same failed connection attempt.
        // If reconnect is already scheduled, don't increment or reschedule.
        if (_needsReconnect) {
            return;
        }

        if (_consecutiveFailures < 250) _consecutiveFailures++;
        _disconnectedAt = millis();

        // Mild backoff: 1s -> 2s -> 5s cap
        uint32_t delayMs = MQTT_RECONNECT_BASE_MS << (_consecutiveFailures - 1);
        if (delayMs > MQTT_RECONNECT_MAX_MS) {
            delayMs = MQTT_RECONNECT_MAX_MS;
        }

        _nextReconnectAt = millis() + delayMs;
        _needsReconnect = true;
        Serial.printf("MQTT retry in %lu ms (attempt %u)\n",
                      (unsigned long)delayMs, _consecutiveFailures);
    }

    void subscribeConfiguredTopics() {
        if (!_configMgr) {
            return;
        }

        char uniqueTopics[MAX_SUBSCRIBED_TOPICS][64];
        uint8_t uniqueCount = 0;

        for (uint8_t i = 0; i < _configMgr->config.subscribedTopicCount; i++) {
            const char* topic = _configMgr->config.subscribedTopics[i];
            if (!isValidSenderMetricsTopic(topic)) {
                Serial.printf("Skip invalid sender topic: %s\n", topic);
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < uniqueCount; j++) {
                if (strcmp(uniqueTopics[j], topic) == 0) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                continue;
            }

            if (uniqueCount >= MAX_SUBSCRIBED_TOPICS) {
                Serial.println("Skip sender topic: allowlist full");
                break;
            }

            strlcpy(uniqueTopics[uniqueCount], topic, sizeof(uniqueTopics[uniqueCount]));
            uniqueCount++;
        }

        if (!shouldSubscribeAnySenderTopic(uniqueCount)) {
            const char* discoveryTopic = _configMgr->config.mqttTopic;
            if (!isValidSenderWildcardMetricsTopic(discoveryTopic)) {
                discoveryTopic = MQTT_SENDER_DISCOVERY_TOPIC;
            }

            _client.subscribe(discoveryTopic, 0);
            Serial.printf("Subscribed discovery topic: %s\n", discoveryTopic);
            return;
        }

        for (uint8_t i = 0; i < uniqueCount; i++) {
            _client.subscribe(uniqueTopics[i], 0);
            Serial.printf("Subscribed sender topic: %s\n", uniqueTopics[i]);
        }
    }
};

#endif
