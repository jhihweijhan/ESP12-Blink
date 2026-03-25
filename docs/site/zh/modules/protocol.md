# 協定模組

Metrics v2 協定定義了發送器與 ESP8266 韌體之間的 MQTT topic 格式和 JSON payload 結構。

## Topic 格式

```
sys/agents/{hostname}/metrics/v2
```

- `{hostname}` 是發送器的主機名稱（或 `SENDER_HOSTNAME` 覆寫值）
- 韌體從 topic 路徑（非 payload）提取主機名稱，做為信任邊界
- 萬用字元訂閱：`sys/agents/+/metrics/v2` 用於自動發現裝置

## Payload 結構

精簡 JSON，使用固定鍵和位置陣列：

```json
{
  "v": 2,
  "ts": 1739999999000,
  "h": "desk",
  "cpu": [42.4, 58.2],
  "ram": [67.8, 12288, 32768],
  "gpu": [15.0, 52.0, 12.5, 0.0, 0.0],
  "net": [1024, 512],
  "disk": [2048, 1024]
}
```

## 欄位對照表

### 頂層欄位

| 鍵 | 型別 | 說明 |
|----|------|------|
| `v` | int | Schema 版本，必須為 `2` |
| `ts` | int | 發送器 Unix epoch（毫秒）|
| `h` | string | 發送器主機名稱 |

### CPU 陣列

`cpu: [cpu_percent, cpu_temp_c]`

| 索引 | 型別 | 單位 | 說明 |
|------|------|------|------|
| 0 | float | % | CPU 使用率（0-100）|
| 1 | float | C | CPU 溫度（攝氏）|

### RAM 陣列

`ram: [ram_percent, ram_used_mb, ram_total_mb]`

| 索引 | 型別 | 單位 | 說明 |
|------|------|------|------|
| 0 | float | % | RAM 使用率（0-100）|
| 1 | int | MB | 目前使用的 RAM |
| 2 | int | MB | 總安裝 RAM |

### GPU 陣列

`gpu: [gpu_percent, gpu_temp_c, gpu_mem_percent, gpu_hotspot_c, gpu_mem_temp_c]`

| 索引 | 型別 | 單位 | 說明 |
|------|------|------|------|
| 0 | float | % | GPU 使用率（0-100）|
| 1 | float | C | GPU 核心溫度 |
| 2 | float | % | GPU 記憶體使用率 |
| 3 | float | C | GPU 熱點溫度（不可用時為 0）|
| 4 | float | C | GPU 記憶體溫度（不可用時為 0）|

### 網路陣列

`net: [rx_kbps, tx_kbps]`

| 索引 | 型別 | 單位 | 說明 |
|------|------|------|------|
| 0 | int | kbps | 網路接收速率 |
| 1 | int | kbps | 網路傳送速率 |

### 磁碟陣列

`disk: [read_kBps, write_kBps]`

| 索引 | 型別 | 單位 | 說明 |
|------|------|------|------|
| 0 | int | kB/s | 磁碟讀取速率 |
| 1 | int | kB/s | 磁碟寫入速率 |

## x10 定點編碼（韌體端）

ESP8266 韌體以 `int16_t` 乘 10 儲存浮點值，避免浮點運算：

| 發送器值 | 韌體儲存 | 顯示值 |
|---------|---------|--------|
| `42.4`（CPU %）| `424`（int16_t）| `42%` |
| `58.2`（CPU 溫度）| `582`（int16_t）| `58C` |
| `67.8`（RAM %）| `678`（int16_t）| `68%` |

`metrics_v2.h` 中的轉換輔助函式：

- `roundedPercent(424)` 回傳 `42`
- `roundedTempC(582)` 回傳 `58`

## 規則

1. 韌體僅接受符合 `/metrics/v2` 的 topic
2. 韌體拒絕 `v != 2` 的 payload
3. 建議發送頻率：每主機 1Hz
4. 發送器應始終傳送所有陣列；不可用的值使用 `0`
5. 最大 payload 大小：1024 位元組（`MQTT_MAX_PAYLOAD_BYTES`）
6. 最大監控裝置數：8（`MAX_DEVICES`）

## 相關頁面

- [系統架構](../../architecture.md) -- 完整系統資料流
- [韌體](firmware.md) -- 韌體如何解析和顯示指標
- [發送器](sender.md) -- 發送器如何收集和發布指標
