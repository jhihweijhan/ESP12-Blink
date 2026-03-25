# Mochi-Metrics

**IoT 系統監控方案 -- ESP8266 TFT 顯示器**

Mochi-Metrics 是一套 IoT 系統監控方案：桌面端發送器（Python/Go）收集 CPU、RAM、GPU、網路及磁碟指標，透過 MQTT 傳送至 ESP8266 韌體，韌體解析後即時顯示在 240x240 TFT 螢幕上。

## 系統總覽

```
主機指標 --> Python/Go 發送器 --> MQTT Broker --> ESP8266 韌體 --> TFT 顯示器
```

- **發送器** 使用 `psutil`（Python）或 `gopsutil`（Go）收集系統指標，以 1Hz 頻率發布精簡 JSON 訊息
- **MQTT Broker** 透過 topic 模式 `sys/agents/{hostname}/metrics/v2` 路由訊息
- **ESP8266 韌體** 訂閱 MQTT topic，解析指標資料，使用髒區域最佳化驅動 TFT 顯示
- **Web UI** 提供 ESP8266 上的設定介面，透過 REST API（ESPAsyncWebServer）操作

## 快速連結

- [系統架構](../architecture.md) -- 完整系統資料流 Mermaid 架構圖
- [學習路線圖](../learning-path.md) -- 三條學習路線涵蓋嵌入式 IoT、網路通訊、系統監控
- [模組教學](../modules/firmware.md) -- 深入各元件模組

## 開始使用

請參考 [安裝與使用指南](https://github.com/jhihweijhan/Mochi-Metrics) 進行環境設定。
