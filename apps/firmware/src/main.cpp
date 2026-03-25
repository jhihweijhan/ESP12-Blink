#include <Arduino.h>

#include "include/device_store.h"
#include "include/monitor_config.h"
#include "include/monitor_display.h"
#include "include/mqtt_transport.h"
#include "include/qr_display.h"
#include "include/tft_driver.h"
#include "include/web_server.h"
#include "include/wifi_manager.h"

TFTDriver tft;
QRDisplay qr(tft);
WiFiManager wifiMgr;
WebServerManager* webServer = nullptr;
MonitorConfigManager monitorConfig;
DeviceStore deviceStore;
MQTTTransport mqttTransport;
MonitorDisplay* monitorDisplay = nullptr;
unsigned long lastHeapLogAt = 0;

void onMqttMetricsReceived(const char* hostname) {
    if (monitorDisplay) {
        monitorDisplay->notifyMetricsUpdated(hostname);
    }
}

enum AppMode {
    MODE_AP_SETUP,
    MODE_MONITOR
};

AppMode currentMode = MODE_AP_SETUP;

enum StartupState {
    STARTUP_INIT = 0,
    STARTUP_TRY_SAVED_START,
    STARTUP_TRY_SAVED_WAIT,
    STARTUP_TRY_SAVED_DELAY,
    STARTUP_TRY_SDK_START,
    STARTUP_TRY_SDK_WAIT,
    STARTUP_TRY_SDK_DELAY,
    STARTUP_WIFI_CONNECTED_DELAY,
    STARTUP_RESET_COUNTDOWN,
    STARTUP_ENTER_AP,
    STARTUP_DONE
};

StartupState startupState = STARTUP_INIT;
uint8_t savedConnectAttempts = 0;
uint8_t sdkConnectAttempts = 0;
uint8_t startupRecoveryCycles = 0;
unsigned long startupNextAt = 0;
bool hasSavedWiFiConfig = false;
bool wifiStorageReady = false;

const uint8_t MAX_SAVED_CONNECT_ATTEMPTS = 3;
const uint8_t MAX_SDK_CONNECT_ATTEMPTS = 2;
const uint8_t RESET_COUNTDOWN_SECONDS = 10;
unsigned long resetCountdownStartAt = 0;
int8_t lastCountdownDisplayed = -1;

void scheduleStartupRetryCycle() {
    startupRecoveryCycles++;
    savedConnectAttempts = 0;
    sdkConnectAttempts = 0;
    startupNextAt = millis() + 2000;
    startupState = STARTUP_TRY_SAVED_DELAY;
    Serial.printf("WiFi retries exhausted, cycle %u/%u\n", startupRecoveryCycles,
                  MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG);
}

void showAPScreen() {
    tft.fillScreen(COLOR_BLACK);

    tft.drawStringCentered(10, "WiFi Setup", COLOR_CYAN, COLOR_BLACK, 2);

    String apSSID = wifiMgr.getAPSSID();
    tft.drawStringCentered(45, apSSID.c_str(), COLOR_WHITE, COLOR_BLACK, 1);

    qr.drawWiFiQR(apSSID.c_str(), nullptr, 10);

    tft.drawStringCentered(210, wifiMgr.localIP.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
}

void showConnectedScreen() {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(10, "Connected", COLOR_GREEN, COLOR_BLACK, 2);
    tft.drawStringCentered(45, wifiMgr.ssid.c_str(), COLOR_WHITE, COLOR_BLACK, 1);

    String url = "http://" + wifiMgr.localIP + "/monitor";
    qr.drawURLQR(url.c_str(), 10);

    tft.drawStringCentered(210, wifiMgr.localIP.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
}

void showConnectingScreen(uint8_t attempt = 0, uint8_t maxAttempts = 0) {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(90, "Connecting", COLOR_CYAN, COLOR_BLACK, 2);
    tft.drawStringCentered(120, wifiMgr.ssid.c_str(), COLOR_WHITE, COLOR_BLACK, 1);
    if (attempt > 0 && maxAttempts > 0) {
        char buf[20];
        snprintf(buf, sizeof(buf), "Attempt %u/%u", attempt, maxAttempts);
        tft.drawStringCentered(150, buf, COLOR_GRAY, COLOR_BLACK, 1);
    }
    if (startupRecoveryCycles > 0) {
        char buf2[20];
        snprintf(buf2, sizeof(buf2), "Cycle %u/%u", startupRecoveryCycles + 1,
                 MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG);
        tft.drawStringCentered(170, buf2, COLOR_GRAY, COLOR_BLACK, 1);
    }
}

void showResetCountdownScreen(uint8_t secondsLeft) {
    if (lastCountdownDisplayed == -1) {
        // 首次繪製：清屏 + 靜態文字
        tft.fillScreen(COLOR_BLACK);
        tft.drawStringCentered(50, "WiFi Failed", COLOR_RED, COLOR_BLACK, 2);
        tft.drawStringCentered(85, "Auto-reset in", COLOR_WHITE, COLOR_BLACK, 1);
        tft.drawStringCentered(170, "Power off to cancel", COLOR_GRAY, COLOR_BLACK, 1);
    }
    // 只更新倒數數字（清除舊數字區域）
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", secondsLeft);
    tft.fillRect(0, 105, 240, 50, COLOR_BLACK);
    tft.drawStringCentered(110, buf, COLOR_YELLOW, COLOR_BLACK, 3);
    lastCountdownDisplayed = secondsLeft;
}

void showMQTTConnectingScreen() {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(80, "MQTT v2", COLOR_CYAN, COLOR_BLACK, 2);
    tft.drawStringCentered(110, "Connecting...", COLOR_WHITE, COLOR_BLACK, 1);
    tft.drawStringCentered(150, monitorConfig.config.mqttServer, COLOR_GRAY, COLOR_BLACK, 1);
}

void ensureWebServer() {
    if (webServer) {
        return;
    }

    webServer = new WebServerManager(wifiMgr);
    webServer->setMonitorConfig(&monitorConfig);
    webServer->setMQTTTransport(&mqttTransport);
    webServer->setDeviceStore(&deviceStore);
    webServer->begin();
}

void startMonitorMode() {
    currentMode = MODE_MONITOR;

    deviceStore.begin();
    mqttTransport.begin(monitorConfig, deviceStore);
    mqttTransport.onMetricsReceived = onMqttMetricsReceived;

    if (strlen(monitorConfig.config.mqttServer) > 0) {
        showMQTTConnectingScreen();
        mqttTransport.connect();
    }

    if (!monitorDisplay) {
        monitorDisplay = new MonitorDisplay(tft, deviceStore, mqttTransport, monitorConfig);
        monitorDisplay->begin();
    }

    ensureWebServer();

    Serial.println("Monitor mode started");
    Serial.printf("WebUI: http://%s/monitor\n", wifiMgr.localIP.c_str());
    startupState = STARTUP_DONE;
}

void startAPMode() {
    currentMode = MODE_AP_SETUP;
    Serial.println("Entering AP mode");

    wifiMgr.startAP();
    showAPScreen();
    wifiMgr.startScan();

    ensureWebServer();
    startupState = STARTUP_DONE;
}

void processStartup() {
    switch (startupState) {
        case STARTUP_TRY_SAVED_START:
            if (!hasSavedWiFiConfig) {
                startupState = STARTUP_TRY_SDK_START;
                break;
            }
            if (savedConnectAttempts >= MAX_SAVED_CONNECT_ATTEMPTS) {
                Serial.println("Saved WiFi found but direct connect failed");
                startupState = STARTUP_TRY_SDK_START;
                break;
            }
            savedConnectAttempts++;
            showConnectingScreen(savedConnectAttempts, MAX_SAVED_CONNECT_ATTEMPTS);
            Serial.printf("WiFi connect attempt %u/%u (from /wifi.json)\n", savedConnectAttempts,
                          MAX_SAVED_CONNECT_ATTEMPTS);
            if (!wifiMgr.startConnectWiFi()) {
                startupState = STARTUP_TRY_SDK_START;
                break;
            }
            startupState = STARTUP_TRY_SAVED_WAIT;
            break;

        case STARTUP_TRY_SAVED_WAIT: {
            WiFiManager::ConnectResult result = wifiMgr.pollConnect();
            if (result == WiFiManager::CONNECT_SUCCESS) {
                startupRecoveryCycles = 0;
                showConnectedScreen();
                startupNextAt = millis() + 2000;
                startupState = STARTUP_WIFI_CONNECTED_DELAY;
            } else if (result == WiFiManager::CONNECT_TIMEOUT || result == WiFiManager::CONNECT_FAILED) {
                if (savedConnectAttempts < MAX_SAVED_CONNECT_ATTEMPTS) {
                    startupNextAt = millis() + 1000;
                    startupState = STARTUP_TRY_SAVED_DELAY;
                } else {
                    startupState = STARTUP_TRY_SDK_START;
                }
            }
            break;
        }

        case STARTUP_TRY_SAVED_DELAY:
            if ((long)(millis() - startupNextAt) >= 0) {
                startupState = STARTUP_TRY_SAVED_START;
            }
            break;

        case STARTUP_TRY_SDK_START:
            if (sdkConnectAttempts >= MAX_SDK_CONNECT_ATTEMPTS) {
                if (shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig, wifiStorageReady, startupRecoveryCycles)) {
                    resetCountdownStartAt = millis();
                    lastCountdownDisplayed = -1;
                    startupState = STARTUP_RESET_COUNTDOWN;
                } else {
                    scheduleStartupRetryCycle();
                }
                break;
            }
            sdkConnectAttempts++;
            showConnectingScreen(sdkConnectAttempts, MAX_SDK_CONNECT_ATTEMPTS);
            Serial.printf("WiFi connect attempt %u/%u (from SDK)\n", sdkConnectAttempts,
                          MAX_SDK_CONNECT_ATTEMPTS);
            if (!wifiMgr.startConnectStoredWiFi()) {
                if (shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig, wifiStorageReady, startupRecoveryCycles)) {
                    resetCountdownStartAt = millis();
                    lastCountdownDisplayed = -1;
                    startupState = STARTUP_RESET_COUNTDOWN;
                } else {
                    scheduleStartupRetryCycle();
                }
                break;
            }
            startupState = STARTUP_TRY_SDK_WAIT;
            break;

        case STARTUP_TRY_SDK_WAIT: {
            WiFiManager::ConnectResult result = wifiMgr.pollConnect();
            if (result == WiFiManager::CONNECT_SUCCESS) {
                startupRecoveryCycles = 0;
                showConnectedScreen();
                startupNextAt = millis() + 2000;
                startupState = STARTUP_WIFI_CONNECTED_DELAY;
            } else if (result == WiFiManager::CONNECT_TIMEOUT || result == WiFiManager::CONNECT_FAILED) {
                if (sdkConnectAttempts < MAX_SDK_CONNECT_ATTEMPTS) {
                    startupNextAt = millis() + 1000;
                    startupState = STARTUP_TRY_SDK_DELAY;
                } else {
                    if (shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig, wifiStorageReady,
                                                          startupRecoveryCycles)) {
                        resetCountdownStartAt = millis();
                        lastCountdownDisplayed = -1;
                        startupState = STARTUP_RESET_COUNTDOWN;
                    } else {
                        scheduleStartupRetryCycle();
                    }
                }
            }
            break;
        }

        case STARTUP_TRY_SDK_DELAY:
            if ((long)(millis() - startupNextAt) >= 0) {
                startupState = STARTUP_TRY_SDK_START;
            }
            break;

        case STARTUP_WIFI_CONNECTED_DELAY:
            if ((long)(millis() - startupNextAt) >= 0) {
                startMonitorMode();
            }
            break;

        case STARTUP_RESET_COUNTDOWN: {
            unsigned long elapsed = millis() - resetCountdownStartAt;
            uint8_t secondsLeft = (elapsed >= RESET_COUNTDOWN_SECONDS * 1000UL)
                ? 0
                : RESET_COUNTDOWN_SECONDS - (uint8_t)(elapsed / 1000UL);
            if (secondsLeft != lastCountdownDisplayed) {
                showResetCountdownScreen(secondsLeft);
            }
            if (secondsLeft == 0) {
                // 倒數結束：清除 WiFi 設定，直接進入 AP 模式（不重啟，避免 SDK 殘留設定循環）
                Serial.println("Countdown finished — clearing WiFi config, entering AP mode");
                LittleFS.remove("/wifi.json");
                WiFi.disconnect(true);   // true = 同時清除 SDK 儲存的設定
                WiFi.mode(WIFI_OFF);
                delay(100);
                hasSavedWiFiConfig = false;
                startupState = STARTUP_ENTER_AP;
            }
            break;
        }

        case STARTUP_ENTER_AP:
            startAPMode();
            break;

        case STARTUP_DONE:
        case STARTUP_INIT:
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP12 System Monitor v2 ===");

    tft.begin();
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(110, "Starting...", COLOR_WHITE, COLOR_BLACK, 2);

    wifiMgr.begin();

    monitorConfig.begin();
    monitorConfig.load();

    wifiStorageReady = wifiMgr.isStorageReady();
    hasSavedWiFiConfig = wifiMgr.loadConfig();
    if (hasSavedWiFiConfig) {
        showConnectingScreen();
        startupState = STARTUP_TRY_SAVED_START;
    } else if (!wifiStorageReady) {
        Serial.println("LittleFS unavailable, cannot load /wifi.json");
        showConnectingScreen();
        startupState = STARTUP_TRY_SDK_START;
    } else {
        Serial.println("No valid /wifi.json, fallback to SDK saved credentials");
        showConnectingScreen();
        startupState = STARTUP_TRY_SDK_START;
    }
}

void loop() {
    unsigned long now = millis();

    if (startupState != STARTUP_DONE) {
        processStartup();
        if (webServer) {
            webServer->loop();
        }
        yield();
        delay(2);
        return;
    }

    if (webServer) {
        webServer->loop();
    }

    if (currentMode == MODE_MONITOR) {
        mqttTransport.loop();
        delay(1);
        monitorConfig.loop();
        if (monitorDisplay) {
            monitorDisplay->loop();
        }
        delay(1);

        // Heap fragmentation monitoring (STAB-01)
        if (now - lastHeapLogAt >= HEAP_LOG_INTERVAL_MS) {
            lastHeapLogAt = now;
            uint32_t freeHeap = ESP.getFreeHeap();
            uint32_t maxBlock = ESP.getMaxFreeBlockSize();
            uint8_t frag = computeHeapFragmentation(freeHeap, maxBlock);
            Serial.printf("HEAP free=%u max_block=%u frag=%u%%\n", freeHeap, maxBlock, frag);
            if (maxBlock < HEAP_WARN_MIN_BLOCK_BYTES) {
                Serial.printf("HEAP WARNING: max_block %u < %u threshold\n", maxBlock, (uint32_t)HEAP_WARN_MIN_BLOCK_BYTES);
            }
        }
    }

    delay(1);
}
