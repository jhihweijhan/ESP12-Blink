<p align="center">
  <img src="https://readme-typing-svg.demolab.com?font=Noto+Sans+TC&weight=900&size=40&pause=1000&color=7C3AED&center=true&vCenter=true&width=600&lines=%E9%BA%BB%E7%B3%AC+Metrics;%E5%B5%8C%E5%85%A5%E5%BC%8F+IoT+%E7%9B%A3%E6%8E%A7%E6%95%99%E5%AD%B8" alt="Mochi Metrics" />
</p>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/%E6%95%99%E5%AD%B8%E5%B0%88%E6%A1%88-Learning%20Project-FF6B6B?style=for-the-badge" /></a>
  <img src="https://img.shields.io/badge/C++-Arduino%20ESP8266-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" />
  <img src="https://img.shields.io/badge/Python-3.10+-3776AB?style=for-the-badge&logo=python&logoColor=white" />
  <img src="https://img.shields.io/badge/Go-1.22-00ADD8?style=for-the-badge&logo=go&logoColor=white" />
  <img src="https://img.shields.io/badge/MQTT-IoT%20Protocol-660066?style=for-the-badge" />
</p>

> 用三種語言實作同一份協定，把電腦指標即時顯示在 ESP8266 小螢幕上 -- 學嵌入式、學協定設計、學效能優化，一個專案全包。

![執行畫面](docs/images/runtime-screen.jpeg)

---

## 這個專案能教你什麼

```
    ┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
    │  嵌入式系統      │ ──→ │  協定設計        │ ──→ │  效能優化        │
    │  Embedded       │     │  Protocol       │     │  Performance    │
    │                 │     │                 │     │                 │
    │ 定點數運算       │     │ MQTT pub/sub    │     │ SPI 批量渲染     │
    │ 髒位追蹤重繪     │     │ 跨語言 JSON     │     │ 指數退避重連     │
    │ 80KB RAM 求生術  │     │ 協定版本管理     │     │ 自適應刷新率     │
    └─────────────────┘     └─────────────────┘     └─────────────────┘
```

<table>
<tr>
<td width="33%" valign="top">

**嵌入式系統設計**

- x10 定點數取代浮點運算
- 16-bit dirty mask 追蹤變更
- 固定大小 struct（48 bytes）避免 heap 碎片
- 協作式單線程事件迴圈
- LittleFS 延遲寫入保護 Flash

</td>
<td width="33%" valign="top">

**跨語言協定設計**

- C++ / Python / Go 三端實作同一份 metrics v2
- MQTT topic 結構與 wildcard 訂閱
- 緊湊陣列 JSON（省頻寬 vs 可讀性取捨）
- hostname 從 topic 萃取（信任邊界設計）
- schema 版本驗證與向前相容

</td>
<td width="33%" valign="top">

**效能優化實戰**

- SPI 行緩衝批量寫入（10-20x 加速）
- 三層刷新率：90ms / 500ms / 1000ms
- 指數退避 + 隨機抖動（1s~60s）
- Heap 碎片監控公式
- 非同步 MQTT 消除 UI 凍結

</td>
</tr>
</table>

| 一般教學範例 | 本專案 |
|:---|:---|
| Todo List -- CRUD 而已 | 即時資料流 + 硬體渲染 + 多協定整合 |
| LED 閃爍 -- 太簡單 | 完整 IoT 系統：感測 → 傳輸 → 解析 → 顯示 |
| 單一語言 | C++ / Python / Go 三語言實作同一份規格 |

---

## Quick Start

```bash
# 1. Clone
git clone https://github.com/your-user/Mochi-Metrics.git && cd Mochi-Metrics

# 2. 燒錄韌體到 ESP12F
cd apps/firmware && ~/.platformio/penv/bin/pio run -t upload

# 3. 啟動 Go Sender（需要 MQTT broker + Go 1.22+）
cd apps/sender/go && bash install.sh

# 或啟動 Python Sender
cd apps/sender/python && pip install -e . && python sender_v2.py
```

### Go Sender 一鍵安裝

各平台提供一鍵安裝腳本，自動編譯、安裝、註冊系統服務：

| 平台 | 指令 | 服務管理 |
|------|------|---------|
| Linux | `bash apps/sender/go/install.sh` | systemd user service |
| macOS | `bash apps/sender/go/install-macos.sh` | launchd agent |
| Windows | `powershell apps/sender/go/install.ps1` (**以系統管理員執行**) | Scheduled Task |

所有腳本支援 `--mqtt-host=IP`（Windows: `-MqttHost IP`）參數和 `--uninstall`（Windows: `-Uninstall`）卸載。

---

## Learning Roadmap

```
Level 1 ── Read（看懂結構）
│
├── 讀 docs/protocol/metrics-v2.md，理解 MQTT topic 與 JSON 格式
├── 讀 connection_policy.h，看純邏輯如何與硬體解耦
└── 讀 metrics_payload.py，比較 Python dataclass 與 Go struct

Level 2 ── Understand（理解原理）
│
├── 追蹤 scaleX10() → roundedPercent()，理解定點數轉換
├── 追蹤 DeviceStore::updateFrame() 的 dirty mask 位運算
└── 比較 PubSubClient（同步）vs espMqttClientAsync（非同步）差異

Level 3 ── Trace（追蹤完整流程）
│
├── 從 sender buildPayload() → MQTT publish → firmware onMessage → parseMetricsV2Payload → updateFrame → display
└── 從 WiFi 斷線 → StartupState FSM → AP 模式 → WebUI 輸入密碼 → 重新連線

Level 4 ── Modify（動手改）
│
├── 練習 1：新增一個 metrics 欄位（如 uptime），三端都要改
├── 練習 2：把 dirty mask 擴充為 32-bit，支援更多指標群組
└── 練習 3：寫一個 Rust sender，實作同一份 metrics v2 協定
```

---

<details>
<summary><b>嵌入式記憶體設計</b>（點擊展開）</summary>

### 定點數 x10 縮放

ESP8266 上浮點運算昂貴且容易產生 heap 碎片。本專案用 `int16_t` 存放 x10 的值：

```cpp
// metrics_v2.h -- 42.3% 存為 423
inline long scaleX10(float value) {
    return lroundf(value * 10.0f);
}
inline int roundedPercent(int16_t pctX10) {
    return (pctX10 + 5) / 10;  // 四捨五入還原
}
```

**學到什麼：** 嵌入式環境下用整數模擬小數的經典手法，遊戲引擎也常用。

### MetricsFrameV2 固定大小結構

```cpp
// metrics_v2.h -- 整個 struct 48 bytes，可直接 memcmp
struct MetricsFrameV2 {
    int16_t cpuPctX10, cpuTempCX10;    // CPU
    int16_t ramPctX10, ramUsedMB;       // RAM
    int16_t gpuPctX10, gpuTempCX10;    // GPU
    uint16_t netRxKbps, netTxKbps;     // Network
};
```

**學到什麼：** 固定大小避免動態分配，在 80KB RAM 裝置上至關重要。

### Dirty Mask 位元追蹤

```cpp
// metrics_v2.h
enum MetricDirtyMask : uint16_t {
    DIRTY_CPU = 1 << 0,   DIRTY_RAM = 1 << 1,
    DIRTY_GPU = 1 << 2,   DIRTY_NET = 1 << 3,
    DIRTY_DISK = 1 << 4,  DIRTY_ONLINE = 1 << 5,
};
```

**學到什麼：** 用 bitmask 追蹤哪些欄位改變，只重繪必要區域 -- React Virtual DOM 的嵌入式版本。

</details>

<details>
<summary><b>跨語言協定實作</b>（點擊展開）</summary>

### 同一份規格，三種語言

MQTT Topic: `sys/agents/<hostname>/metrics/v2`

```json
{"v":2, "ts":1739999999000, "h":"desk",
 "cpu":[42.4, 58.2], "ram":[67.8, 12288, 32768]}
```

**Python** -- dataclass + 字典：
```python
# metrics_payload.py
@dataclass
class CpuSnapshot:
    percent: float; temp_c: float | None
```

**Go** -- struct + JSON tag：
```go
// payload.go
type Payload struct {
    V   int       `json:"v"`
    CPU []float64 `json:"cpu"`
}
```

**C++ 韌體** -- 從 JSON 陣列逐位置解析：
```cpp
// metrics_parser_v2.h
frame.cpuPctX10 = scaleX10(doc["cpu"][0]);
frame.cpuTempCX10 = scaleX10(doc["cpu"][1]);
```

**學到什麼：** 協定設計讓不同語言的實作完全解耦；只要 JSON 結構一致，任何語言都能加入。

</details>

<details>
<summary><b>連線恢復策略</b>（點擊展開）</summary>

### 指數退避 + 隨機抖動

```cpp
// connection_policy.h
uint32_t computeMqttReconnectDelayMs(uint8_t failureCount) {
    uint32_t delay = MQTT_RECONNECT_BASE_MS << failureCount; // 1s→2s→4s→8s...
    return min(delay, MQTT_RECONNECT_MAX_MS);                // 上限 60s
}
```

**學到什麼：** 所有分散式系統都需要退避策略，AWS/GCP SDK 也用同樣的模式。

### WiFi 啟動狀態機

```
INIT → TRY_SAVED(x3) → TRY_SDK(x2) → [12 cycles?] → AP_MODE → WebUI 設定 → DONE
```

**學到什麼：** 有限狀態機（FSM）管理多階段恢復流程，比巢狀 if-else 清晰得多。

</details>

<details>
<summary><b>SPI 渲染優化</b>（點擊展開）</summary>

### 從逐像素到批量寫入

```cpp
// 優化前：每像素 2 次 SPI.transfer()
for (int i = 0; i < 256; i++) {
    SPI.transfer(hi); SPI.transfer(lo);
}

// 優化後：行緩衝一次寫入
uint8_t buf[32];  // 16 pixels x 2 bytes
SPI.writeBytes(buf, 32);  // 1 次 DMA 交易
```

效果：字元渲染速度提升 **10-20 倍**，SPI 時脈從 10MHz 升至 40MHz。

**學到什麼：** I/O 批量化是最基本也最有效的效能優化手法。

### 三層自適應刷新

```cpp
// connection_policy.h
if (forceRedraw) return 90;             // 新裝置切入
return hasPendingUpdate ? 500 : 1000;   // 有更新快刷，無更新慢刷
```

**學到什麼：** 根據系統狀態動態調整更新頻率，平衡回應速度與功耗。

</details>

<details>
<summary><b>專案架構總覽</b>（點擊展開）</summary>

### 系統資料流

```
  [Host PC]                    [MQTT Broker]              [ESP8266]
  ┌──────────┐   publish       ┌───────────┐   subscribe  ┌──────────────┐
  │ Python   │ ──────────────→ │ Mosquitto │ ───────────→ │ mqtt_transport│
  │ Go       │  metrics/v2     │           │              │ → parser     │
  │ Sender   │                 └───────────┘              │ → device_store│
  └──────────┘                                            │ → display    │
   psutil/gopsutil                                        │ → TFT 240x240│
                                                          └──────────────┘
```

### Header-Only 架構

所有韌體模組都是 `.h` 檔（無 `.cpp`），在 `main.cpp` 單一編譯單元中 include：

```
firmware/
├── include/connection_policy.h    ← 純邏輯（可 native 測試）
├── src/include/
│   ├── mqtt_transport.h           ← MQTT 非同步傳輸
│   ├── device_store.h             ← 裝置資料儲存
│   ├── monitor_display.h          ← TFT 顯示邏輯
│   ├── metrics_parser_v2.h        ← JSON 解析
│   └── ...（9 個 header）
└── src/main.cpp                   ← 唯一的 .cpp
```

**學到什麼：** Header-only 簡化了 PlatformIO 建置，但犧牲了增量編譯速度 -- 理解取捨很重要。

</details>

---

## MQTT 規格

- Topic: `sys/agents/<hostname>/metrics/v2`
- Payload 規格: [docs/protocol/metrics-v2.md](docs/protocol/metrics-v2.md)

## 硬體型號資訊

| 元件 | 型號 |
|---|---|
| LCD | `BL-A54038P-02`（ST7789, 240x240, SPI） |
| Wi-Fi 模組 | `ESP-12F`（ESP8266, 80KB RAM, 4MB Flash） |

## 教學文件站

本專案附帶完整的教學文件站（MkDocs Material），含系統架構圖、學習路線圖、各模組教學。

```bash
# 本地預覽文件站
cd docs && uv sync && uv run mkdocs serve
# 瀏覽 http://127.0.0.1:8000
```

支援繁體中文 / English 雙語切換。

## 開發者命令速查

```bash
# 韌體
cd apps/firmware
~/.platformio/penv/bin/pio run           # 編譯
~/.platformio/penv/bin/pio test -e native # 單元測試

# Python Sender
cd apps/sender/python
uv sync --extra dev && uv run python -m pytest -q

# Go Sender
cd apps/sender/go
go test ./... && go build -o sender_v2 .

# 文件站
cd docs
uv sync && uv run mkdocs serve           # 本地預覽
uv run mkdocs build                      # 建置靜態站
```

## License

[Commons Clause](LICENSE-COMMONS-CLAUSE.md) + [AGPL-3.0](LICENSE)
