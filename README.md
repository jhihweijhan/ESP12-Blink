<p align="center">
  <img src="https://readme-typing-svg.demolab.com?font=Noto+Sans+TC&weight=900&size=38&pause=1000&color=0F766E&center=true&vCenter=true&width=860&lines=Mochi-Metrics%EF%BC%9A%E5%BE%9E%E7%B3%BB%E7%B5%B1%E6%8C%87%E6%A8%99%E5%88%B0%E5%B5%8C%E5%85%A5%E5%BC%8F%E9%A1%AF%E7%A4%BA;Compact+Protocol+%C3%97+MQTT+%C3%97+ESP12+%C3%97+Python%2FGo" alt="title" />
</p>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/教學專案-Learning%20Project-FF6B6B?style=for-the-badge" alt="teaching-project" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Python-3.11-3776AB?style=for-the-badge&logo=python&logoColor=white" alt="python" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Go-1.21-00ADD8?style=for-the-badge&logo=go&logoColor=white" alt="go" /></a>
  <a href="#"><img src="https://img.shields.io/badge/MQTT-Metrics%20v2-1D4ED8?style=for-the-badge" alt="mqtt" /></a>
  <a href="#"><img src="https://img.shields.io/badge/ESP12-Firmware-F59E0B?style=for-the-badge" alt="firmware" /></a>
</p>

這不是單純把 CPU / RAM 數字送上螢幕的專案，而是一個很完整的教學案例：你可以一路學到資料建模、跨平台 metrics 蒐集、compact JSON protocol、MQTT 傳輸、ESP 韌體解析與小螢幕 UI 更新。

```text
    ┌────────────────────┐     ┌────────────────────┐     ┌────────────────────┐
    │ Theme A            │ --> │ Theme B            │ --> │ Theme C            │
    │ Data Modeling      │     │ Resilient I/O      │     │ Embedded Rendering │
    │ fixed arrays       │     │ multi-source GPU   │     │ MQTT -> parser     │
    │ versioned payload  │     │ retry + fallback   │     │ device store -> UI │
    └────────────────────┘     └────────────────────┘     └────────────────────┘
```

<table>
  <tr>
    <td valign="top" width="33%">
      <b>Theme A<br>資料模型與協定設計</b>
      <ul>
        <li><code>MetricsSnapshot</code> 用 dataclass 把主機狀態拆成 CPU / RAM / GPU / Net / Disk</li>
        <li><code>build_payload()</code> 把物件壓成固定 key 與固定 array 位置</li>
        <li><code>topic_for_host()</code> 展示 topic naming 與 host 分流</li>
        <li><code>docs/protocol/metrics-v2.md</code> 示範 schema versioning</li>
      </ul>
    </td>
    <td valign="top" width="33%">
      <b>Theme B<br>容錯式系統資訊蒐集</b>
      <ul>
        <li><code>get_gpu_snapshot()</code> 依序嘗試 NVIDIA / ROCm / Linux sysfs</li>
        <li><code>RateSampler.sample()</code> 由 byte counter 推出即時速率</li>
        <li><code>connect_mqtt_with_retry()</code> 做 exponent-like backoff</li>
        <li>測試檔驗證 fallback 與變體欄位解析</li>
      </ul>
    </td>
    <td valign="top" width="33%">
      <b>Theme C<br>從資料流到嵌入式畫面</b>
      <ul>
        <li>Sender 發佈到 <code>sys/agents/&lt;hostname&gt;/metrics/v2</code></li>
        <li>Firmware 以 <code>parseMetricsV2Payload()</code> 驗證與解析</li>
        <li><code>MQTTTransport::handleMessage()</code> 串接 topic allowlist 與 device store</li>
        <li><code>MonitorDisplay::loop()</code> 把 metrics 轉成顯示輪播</li>
      </ul>
    </td>
  </tr>
</table>

## Quick Start

1. `git clone https://github.com/jhihweijhan/Mochi-Metrics.git && cd Mochi-Metrics`
2. `cd apps/sender/python && uv sync --extra dev && uv run python -m pytest -q`
3. `MQTT_HOST=127.0.0.1 MQTT_PORT=1883 uv run python sender_v2.py`

想直接跑整套系統而不是先做教學閱讀，請看 [docs/guide/安裝與使用指南.md](docs/guide/安裝與使用指南.md)。

## Roadmap

```text
Level 1 - Read (看懂結構)
│
├── 先看 apps/sender/python/metrics_payload.py 的 dataclass 版型
├── 再看 docs/protocol/metrics-v2.md 對照 payload 陣列位置
└── 最後掃過 apps/firmware/src/main.cpp 了解 sender -> firmware 雙端角色

Level 2 - Understand (理解原理)
│
├── 讀 apps/sender/python/sender_v2.py::RateSampler.sample()
├── 讀 apps/sender/python/sender_v2.py::get_gpu_snapshot()
└── 讀 apps/firmware/include/connection_policy.h 的 topic / payload 驗證規則

Level 3 - Trace (追蹤流程)
│
├── 從 sender_v2.py::main() 追到 build_snapshot() -> build_payload() -> publish()
├── 從 firmware 的 MQTTTransport::handleMessage() 追到 parseMetricsV2Payload()
└── 再追到 monitor_display.h 的畫面更新與輪播

Level 4 - Modify (動手改)
│
├── 練習新增一個 metrics 欄位，連動 Python sender、protocol doc、firmware parser
├── 練習改寫 GPU fallback 順序或 ranking 邏輯
└── 練習把顯示頁面切成你自己的 metrics 卡片配置
```

<details>
<summary><b>為什麼這個專案比 Todo List 更適合教學</b>（點擊展開）</summary>

| Typical Example | This Project |
|:---|:---|
| Todo List 常只教 CRUD | 這裡同時有資料蒐集、序列化、傳輸、解析、渲染 |
| 單一 runtime，失敗模式很少 | 這裡有 broker、主機、GPU 工具鏈、韌體多層 fallback |
| UI 與資料結構通常鬆散 | 這裡的 fixed arrays 逼你思考 schema 與相容性 |
| 做完就結束 | 你可以持續加欄位、改 topic、改顯示輪播、換 sender 實作 |

這個 repo 的價值在於：它有「真實系統的限制」，但規模仍然小到可以一個人完整追完。這種密度很適合教學。

</details>

<details>
<summary><b>Detail 1: Compact Protocol 與資料模型</b>（點擊展開）</summary>

### 核心片段

```python
return {
    "v": 2,
    "cpu": [round(snapshot.cpu.percent, 1), round(snapshot.cpu.temp_c, 1)],
    "gpu": [round(snapshot.gpu.percent, 1), round(snapshot.gpu.temp_c, 1), round(snapshot.gpu.mem_percent, 1), round(snapshot.gpu.hotspot_c, 1), round(snapshot.gpu.mem_temp_c, 1)],
}
```

What students learn: 物件結構不一定要原封不動送出去；在嵌入式或網路傳輸情境下，固定位置陣列常比冗長 JSON object 更穩定、更省空間，也更適合 versioned protocol。

### 你可以觀察什麼

- `apps/sender/python/metrics_payload.py` 先把資料切成 `CpuSnapshot`、`GpuSnapshot` 等小模型。
- `build_payload()` 再把教學上容易理解的物件，壓成韌體容易處理的 compact schema。
- `docs/protocol/metrics-v2.md` 明確定義每個陣列索引位置，這是 API contract 的最小版本。

</details>

<details>
<summary><b>Detail 2: GPU metrics fallback 是這個專案最值得讀的演算法之一</b>（點擊展開）</summary>

### 核心片段

```python
nvidia_snapshot = _read_gpu_snapshot_nvidia()
if nvidia_snapshot is not None:
    return nvidia_snapshot
rocm_snapshot = _read_gpu_snapshot_rocm()
if rocm_snapshot is not None:
    return rocm_snapshot
```

What students learn: 真實世界的資料來源不會一致。好的程式不是「支援一種格式」，而是能在工具、欄位名稱、作業系統路徑都不同時，仍維持同一個輸出模型。

### 為什麼它有教學味

- ROCm JSON 欄位名稱可能變動，所以 `_read_metric_by_keys()` 用關鍵字包含規則兜底。
- Linux sysfs fallback 不是讀一個檔就結束，還要掃 `card*`、`hwmon*`、label 與溫度來源。
- `_gpu_snapshot_rank()` 顯示了「多張卡片時，怎麼挑最值得顯示的一張」這類 heuristic 設計。
- `apps/sender/python/tests/test_sender_v2_gpu.py` 直接把這些 edge cases 寫成測試，是很好的教學素材。

</details>

<details>
<summary><b>Detail 3: 從 MQTT topic 到 ESP 螢幕，是完整的資料流教學</b>（點擊展開）</summary>

### 核心片段

```cpp
if (!parseMetricsV2Payload(topic, payload, length, hostname, sizeof(hostname), frame)) {
    Serial.printf("Drop invalid metrics v2 payload on topic: %s\n", topic ? topic : "<null>");
    return;
}
```

What students learn: 接收端不能假設資料永遠正確。先做 topic 驗證、長度限制、schema 驗證，再進入 state update，這才是硬體端可靠接收流程。

### 可以一路追的檔案

- `apps/firmware/include/connection_policy.h`：定義 topic prefix/suffix、payload 限制、reconnect policy。
- `apps/firmware/src/include/mqtt_transport.h`：集中處理訂閱、allowlist、message parse、device store 寫入。
- `apps/firmware/src/include/monitor_display.h`：把 metrics frame 變成實際的數字與狀態文字。
- `apps/firmware/src/include/web_server.h`：展示韌體端 WebUI 設定與 JSON API 的最小後台介面。

</details>

<details>
<summary><b>Detail 4: 建議的教學順序與改題方向</b>（點擊展開）</summary>

### 適合帶學生做的 4 個改題

1. 把 `disk` 再細分成 `read_iops / write_iops`，並更新 sender 與 protocol。
2. 在 firmware 新增單機固定頁與多機輪播頁，練習 UI state 切換。
3. 為 `connect_mqtt_with_retry()` 補測試或抽出 retry policy。
4. 比較 `apps/sender/python` 與 `apps/sender/go` 的設計取捨，討論「同一協定、多語言實作」。

### 如果你在帶課

- 先讓學生只改 `metrics_payload.py`，理解資料模型。
- 再讓學生追 `sender_v2.py::main()`，理解 publish pipeline。
- 最後才進 firmware，因為硬體端的 callback、顯示與 WebUI 會同時帶入更多背景知識。

</details>

---

教學提示：先把這個專案當成「資料流系統」來讀，不要急著把它當成「硬體專案」。License 請見 [LICENSE-COMMONS-CLAUSE.md](LICENSE-COMMONS-CLAUSE.md)。
