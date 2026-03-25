# 韌體模組

ESP8266 韌體是 Mochi-Metrics 的核心：接收 MQTT 指標、儲存逐裝置資料，並使用協作式單執行緒事件迴圈在 240x240 TFT 顯示器上渲染。

## 檔案結構

所有韌體模組都是 `apps/firmware/src/include/` 中的標頭檔（單一翻譯單元）：

| 檔案 | 角色 |
|------|------|
| `main.cpp` | 進入點：`setup()` + `loop()` 狀態機 |
| `mqtt_transport.h` | MQTT 連線生命週期、訂閱、訊息分派 |
| `metrics_parser_v2.h` | JSON 解析為 `MetricsFrameV2` 結構 |
| `device_store.h` | 逐裝置指標儲存與髒遮罩追蹤 |
| `monitor_display.h` | TFT 渲染與髒區域最佳化 |
| `monitor_config.h` | 基於 LittleFS 的持久化設定 |
| `web_server.h` | ESPAsyncWebServer REST API |
| `tft_driver.h` | SPI 顯示驅動（ST7789，批次傳輸）|
| `ui_components.h` | 可重用的 UI 繪製元件 |
| `metrics_v2.h` | `MetricsFrameV2` 資料結構定義 |
| `connection_policy.h` | 純邏輯決策函式（可在 native 平台測試）|

## 核心概念

### 協作式迴圈（Cooperative Loop）

ESP8266 運行單執行緒 `loop()` 函式。所有操作必須非阻塞以保持 UI 回應：

```cpp
void loop() {
    mqttTransport.loop();       // 驅動 MQTT 客戶端
    webServer.loop();           // 處理 HTTP 請求
    monitorDisplay.loop();      // 更新 TFT 顯示
    configManager.loop();       // 延遲設定儲存（5s 防抖）
}
```

若任何操作阻塞（例如同步 TCP 連線），整個 UI 會凍結直到完成。

### 髒區域顯示（Dirty-Region Display）

韌體不會每幀重繪整個螢幕，而是追蹤哪些指標群組有變動：

1. `DeviceStore::updateFrame()` 逐欄位比對新舊資料框
2. 設定髒位元：`DIRTY_CPU`、`DIRTY_RAM`、`DIRTY_GPU`、`DIRTY_NET`、`DIRTY_DISK`、`DIRTY_ONLINE`
3. `MonitorDisplay` 僅重繪髒位元設定的列

三階段更新頻率最佳化回應速度：

| 階段 | 間隔 | 時機 |
|------|------|------|
| 強制重繪 | 90ms | 畫面狀態切換（裝置切換）|
| 活躍更新 | 200ms | 有新資料進入 |
| 閒置更新 | 1000ms | 近期無新資料 |

### x10 定點整數

`MetricsFrameV2` 以 `int16_t` 乘 10 儲存百分比和溫度，避免 ESP8266 上的浮點運算：

```cpp
// CPU 使用率 42.3% 儲存為 423
int16_t cpuPercent;  // x10 縮放

// metrics_v2.h 中的轉換輔助函式
int roundedPercent(int16_t x10val);  // 423 -> 42
int roundedTempC(int16_t x10val);    // 582 -> 58
```

### SPI 批次傳輸

TFT 驅動使用批次 SPI 傳輸取代逐像素寫入：

- **64 位元組 SPI 緩衝區** 匹配 ESP8266 FIFO 容量
- `drawCharScaled()` 使用行批次渲染（最大尺寸 4）
- 40MHz SPI 時脈（ESP8266 80/2 除頻器，在 ST7789 規格範圍內）

## 關鍵設計決策

- **純標頭檔架構**：所有 `.h` 檔在單一翻譯單元中編譯。簡化 PlatformIO 建置但增加重建時間。
- **可測試的策略層**：`connection_policy.h` 位於 `include/`（非 `src/include/`），包含可在 native 平台測試的純邏輯函式。
- **espMqttClientAsync + keepalive=0**：PINGREQ 在 ESP8266 上不可靠；韌體改用自訂 30 秒靜默檢查。

## 相關頁面

- [系統架構](../architecture.md) -- 系統層級資料流
- [協定](protocol.md) -- Metrics v2 JSON 規格
- [發送器](sender.md) -- 韌體的資料來源
