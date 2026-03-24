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
            _reconnectFailureCount = 0;
            _lastConnectedAt = millis();
            Serial.println("MQTT connected");
            subscribeConfiguredTopics();
        });

        _client.onDisconnect([this](espMqttClientTypes::DisconnectReason reason) {
            Serial.printf("MQTT disconnected: %u\n", static_cast<uint8_t>(reason));
            if (_state == MqttConnectionState::CONNECTED) {
                _lastDisconnectedAt = millis();
            }
            _state = MqttConnectionState::DISCONNECTED;
            scheduleReconnect();
        });

        _client.onMessage([this](
            const espMqttClientTypes::MessageProperties& properties,
            const char* topic,
            const uint8_t* payload,
            size_t len, size_t index, size_t total) {
            if (index == 0 && len == total) {
                handleMessage(const_cast<char*>(topic),
                              const_cast<uint8_t*>(payload), static_cast<unsigned int>(len));
            }
        });
    }

    void connect() {
        if (!_configMgr || strlen(_configMgr->config.mqttServer) == 0) {
            Serial.println("MQTT server not configured");
            return;
        }

        _client.setServer(_configMgr->config.mqttServer, _configMgr->config.mqttPort);

        String clientId = "ESP12-v2-" + String(random(0xffff), HEX);
        _client.setClientId(clientId.c_str());

        if (strlen(_configMgr->config.mqttUser) > 0) {
            _client.setCredentials(_configMgr->config.mqttUser,
                                   _configMgr->config.mqttPass);
        }

        _state = MqttConnectionState::RECONNECTING;
        _client.connect();
        Serial.printf("Connecting MQTT: %s:%d\n",
                       _configMgr->config.mqttServer,
                       _configMgr->config.mqttPort);
    }

    void loop() {
        if (!_configMgr || !_store || strlen(_configMgr->config.mqttServer) == 0) {
            return;
        }

        unsigned long now = millis();

        if (_needsReconnect && _state == MqttConnectionState::DISCONNECTED) {
            if ((long)(now - _nextReconnectAt) >= 0) {
                _needsReconnect = false;
                connect();
            }
        }

        if (now - _lastOfflineCheckAt >= 1000UL) {
            _lastOfflineCheckAt = now;
            _store->markOfflineExpired(now, getOfflineTimeoutMs());
        }
    }

    MqttConnectionState getConnectionState() const { return _state; }
    bool isConnected() const { return _state == MqttConnectionState::CONNECTED; }

    bool isConnectedForDisplay() const {
        if (_state == MqttConnectionState::CONNECTED) return true;
        if (_state == MqttConnectionState::RECONNECTING) return true;
        unsigned long now = millis();
        return !shouldShowMqttDisconnectedStatus(false, now, _lastConnectedAt, _lastMessageAt);
    }

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
    unsigned long _lastDisconnectedAt = 0;
    MonitorConfigManager* _configMgr = nullptr;
    DeviceStore* _store = nullptr;
    uint8_t _reconnectFailureCount = 0;
    unsigned long _lastRxLogAt = 0;
    uint16_t _rxMessageCount = 0;
    unsigned long _lastConnectedAt = 0;
    unsigned long _lastMessageAt = 0;
    unsigned long _lastOfflineCheckAt = 0;

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
        if (_reconnectFailureCount < 250) _reconnectFailureCount++;
        uint32_t delayMs = computeMqttReconnectDelayMs(_reconnectFailureCount);
        uint32_t jitter = random(0, 500);
        _nextReconnectAt = millis() + delayMs + jitter;
        _needsReconnect = true;
        Serial.printf("MQTT retry in %lu ms\n", (unsigned long)(delayMs + jitter));
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
