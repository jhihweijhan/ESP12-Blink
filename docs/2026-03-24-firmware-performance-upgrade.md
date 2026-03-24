# 韌體效能升級報告（2026-03-24）

## 背景與目標

ESP12 韌體有兩個已知效能瓶頸：
1. **MQTT PubSubClient.connect() 同步阻塞**：伺服器不可達時阻塞 10-30 秒，期間 UI 完全凍結
2. **TFT drawChar() 逐像素繪製**：每個字元 128 次 SPI 交易，渲染效率極低

本次里程碑目標：消除 UI 凍結、大幅提升渲染效能。

## 改善內容

### Phase 1: TFT 批次 SPI 渲染最佳化

- **drawChar() 行批次化**：每行字型資料填入 row buffer 後一次 `SPI.writeBytes()` 傳送，SPI 交易從每字元 128 次降至 16 次
- **drawCharScaled() 放大字元**：size >= 2 的字元同樣使用行批次，取代逐像素 fillRect
- **fillRect/fillScreen 批次填充**：使用 64-byte buffer + `SPI.writeBytes()` 迴圈，取代逐像素 `SPI.transfer()`
- **SPI 時脈提升**：10MHz → 40MHz（ST7789 最大規格，硬體驗證通過）
- **預期效果**：字元渲染 10-20 倍加速

### Phase 2: 非同步 MQTT 遷移

- **PubSubClient → espMqttClientAsync**：connect() 不再阻塞，完全事件驅動
- **Flag-based 自動重連**：斷線時 `onDisconnect` 設定 `_needsReconnect` flag，由 `loop()` 在指數退避延遲後觸發重連
- **三態連線狀態顯示**：TFT footer 顯示 `MQTT OK`（綠）/ `MQTT ..`（黃）/ `MQTT --`（紅）
- **Web API 增強**：`/api/v2/status` 新增 `mqttState` 欄位（connected/reconnecting/disconnected）
- **PubSubClient patch 移除**：`scripts/patch_pubsubclient.py` 已刪除，espMqttClient 內建安全的封包解析

### Phase 3: 穩定性驗證與 Heap 監控

- **Heap 碎片監控**：每 30 秒 Serial 輸出 `HEAP free=X max_block=Y frag=Z%`，低於 4KB 時警告
- **Web API 暴露**：`/api/v2/status` 新增 `freeHeap`、`maxBlock`、`heapFragmentation` 欄位

## 除錯修復（Phase 2/3 硬體驗證中發現）

### 1) clientId 懸空指標

- **現象**：MQTT 不到幾分鐘就斷線
- **根因**：`connect()` 中 `String clientId` 是局部變數，`setClientId()` 只保存指標。espMqttClientAsync 的 `connect()` 是非阻塞的，實際連線在之後才發生，此時局部 String 已被銷毀
- **修復**：改用類別成員 `char _clientId[20]` + `snprintf()`

### 2) MQTT 訊息分片丟棄

- **現象**：MQTT 連線正常但 metrics 資料不更新
- **根因**：`onMessage` 回調只處理 `index == 0 && len == total` 的完整訊息，TCP 分片的訊息被靜默丟棄
- **修復**：新增 1KB 接收緩衝區 `_msgBuf`，將分片訊息重組後再交給 `handleMessage()`

### 3) PINGREQ 未發送導致 broker 主動斷線

- **現象**：每隔約 90 秒 MQTT 斷線（1.5× keepalive）
- **根因**：espMqttClientAsync 在 ESP8266 上無法可靠發送 PINGREQ，Mosquitto 因超時主動斷開連線
- **修復**：`setKeepAlive(0)` 禁用 MQTT keepalive，改用自定義 30 秒沈默偵測（`MQTT_CONNECTED_SILENCE_TIMEOUT_MS`）偵測殭屍連線

### 4) SPI 渲染阻塞 TCP 堆疊

- **現象**：MQTT 保持連線但 metrics 延遲 10-20 秒更新
- **根因**：TFT 渲染佔用大量 CPU，`yield()` 在 ESP8266 上不足以完整處理 WiFi/TCP stack
- **修復**：
  - 所有 `yield()` 改為 `delay(1)` — 強制完整 WiFi/TCP stack 處理
  - 顯示各區段（CPU/RAM/GPU/NET/DISK）之間插入 `delay(1)`
  - `fillScreen` 長迴圈每 64 chunks 插入 `yield()`
  - `DISPLAY_ACTIVE_REFRESH_MS` 從 200ms 提升至 500ms（每秒 2 次刷新）

### 5) Footer 殘留字元

- **現象**：MQTT 狀態顯示為 `oMQTT OK`，多一個 'o'
- **根因**：MQTT 狀態從 x=8 開始繪製，x=0~7 的殘留像素未被清除
- **修復**：起始 x 改為 0，padding 增加到 78px 完全覆蓋

### 6) IP 顯示殘留像素（1920.0.0.0249）

- **現象**：IP 從 `192.168.5.249` 錯亂顯示為 `1920.0.0.0249`
- **根因**：`drawStringCentered()` 不清除舊像素，IP 長度變化時新舊文字重疊
- **修復**：先 `fillRect()` 清除區域再 `drawStringCentered()`，並以差異比較只在值變更時重繪避免閃爍

### 7) espMqttClientAsync 重連後 connect() 返回 false

- **現象**：MQTT 斷線後無法重連，一直卡在 DISCONNECTED
- **根因**：`disconnect(true)` 只將 espMqttClient 內部狀態設為 `disconnectingTcp1`，需要多次 `_client.loop()` 呼叫才能到達 `disconnected`；此前 `connect()` 因 `_state != disconnected` 返回 false
- **修復**：重連前檢查 `_client.disconnected()` 確認 client 已完全斷線，否則等下一次 loop 迭代

### 8) 重連雙重計數導致退避膨脹

- **現象**：MQTT 重連延遲越來越長
- **根因**：RECONNECTING 超時和 `onDisconnect` 回調都呼叫 `scheduleReconnect()`，失敗次數被加倍計算
- **修復**：`scheduleReconnect()` 加入 `_needsReconnect` 防護，已排程時不重複遞增

## 技術數據

| 指標 | 修改前 | 修改後 |
|------|--------|--------|
| SPI 時脈 | 10 MHz | 40 MHz |
| drawChar SPI 交易/字元 | 128 | 16 |
| MQTT connect() 阻塞 | 10-30 秒 | 0（非同步） |
| MQTT keep-alive | 15 秒（預設） | 0（禁用，改用 30s 沈默偵測） |
| 顯示刷新間隔（active） | 200 ms | 500 ms |
| RAM 使用率 | ~45% | ~48.4% |
| Flash 使用率 | ~38% | ~39.6% |
| MQTT 函式庫 | PubSubClient 2.8 | espMqttClient 1.7+ (async) |

## 依賴變更

- **新增**：`bertmelis/espMqttClient` — 非同步 MQTT client
- **移除**：`knolleary/PubSubClient` — 同步 MQTT client
- **移除**：`scripts/patch_pubsubclient.py` — 不再需要的建置時 patch
- **移除**：`platformio.ini` 中的 `extra_scripts` 設定
