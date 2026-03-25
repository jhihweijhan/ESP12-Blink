# 發送器模組

發送器收集主機系統指標並發布到 MQTT broker。目前有兩種實作：Python（跨平台、Docker 就緒）和 Go（單一靜態執行檔）。

## 資料收集鏈

```
作業系統指標 --> 收集器 (psutil/gopsutil) --> buildPayload() --> MQTT 發布
```

### Python 發送器

進入點：`apps/sender/python/sender_v2.py`

```python
# 簡化的資料流
metrics = MetricsSnapshot(
    cpu=CpuSnapshot(percent, temp_c),
    ram=RamSnapshot(percent, used_mb, total_mb),
    gpu=get_gpu_snapshot(),
    net=NetSnapshot(rx_kbps, tx_kbps),
    disk=DiskSnapshot(read_kBps, write_kBps),
)
payload = build_payload(metrics, hostname)
client.publish(f"sys/agents/{hostname}/metrics/v2", json.dumps(payload))
```

關鍵檔案：

| 檔案 | 用途 |
|------|------|
| `sender_v2.py` | 主迴圈：連線 MQTT、收集指標、依間隔發布 |
| `metrics_payload.py` | `build_payload()`：組裝精簡 JSON 位置陣列 |

### Go 發送器

進入點：`apps/sender/go/main.go`

```go
// 簡化的資料流
payload := buildPayload(hostname)
token := client.Publish(
    fmt.Sprintf("sys/agents/%s/metrics/v2", hostname),
    qos, false, payload,
)
```

關鍵檔案：

| 檔案 | 用途 |
|------|------|
| `main.go` | 主迴圈：MQTT 客戶端設定、指標收集、發布迴圈 |
| `payload.go` | `buildPayload()`：組裝 `Payload` 結構含 JSON 標籤 |

## GPU 偵測策略

兩種發送器都使用層疊偵測方式：

```
1. NVIDIA GPU?  --> nvidia-smi CLI --> 解析 XML/CSV 輸出
2. AMD GPU?     --> rocm-smi CLI   --> 解析輸出
3. Linux sysfs? --> /sys/class/drm/card*/device/ --> 讀取 hwmon 溫度
4. 未偵測到    --> 回傳零值 (gpu: [0, 0, 0, 0, 0])
```

層疊偵測在每次收集間隔執行，支援 GPU 熱插拔偵測。

## 環境變數設定

所有設定透過環境變數（不需設定檔）：

| 變數 | 預設值 | 說明 |
|------|--------|------|
| `MQTT_HOST` | `127.0.0.1` | MQTT broker 主機名稱 |
| `MQTT_PORT` | `1883` | MQTT broker 連接埠 |
| `MQTT_USER` | （空）| Broker 驗證使用者名稱 |
| `MQTT_PASS` | （空）| Broker 驗證密碼 |
| `MQTT_QOS` | `0` | MQTT QoS 等級（0、1 或 2）|
| `SENDER_HOSTNAME` | 系統主機名稱 | 覆寫回報的主機名稱 |
| `SEND_INTERVAL_SEC` | `1.0` | 發布頻率（秒）|

## 部署方式

### Docker Compose（Python）

```bash
cd apps/sender/python
./senderctl.sh quickstart-compose
```

`senderctl.sh` 腳本提供：

- `quickstart-compose` -- 互動式設定精靈 + 啟動
- `compose-status` -- 檢查容器狀態
- `compose-logs` -- 檢視發送器日誌
- `compose-restart` -- 重啟發送器
- `ensure-docker-boot` -- 設定開機自動啟動

### 靜態執行檔（Go）

```bash
cd apps/sender/go
go build -o sender-go .
MQTT_HOST=192.168.1.100 ./sender-go
```

Go 發送器編譯為單一靜態執行檔，無外部相依。

### systemd 服務

不使用 Docker 的持久化部署方式：

```ini
[Unit]
Description=Mochi-Metrics Sender
After=network-online.target

[Service]
ExecStart=/path/to/sender-go
Environment=MQTT_HOST=192.168.1.100
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

## 錯誤處理

- **Python**：`connect_mqtt_with_retry()` 使用指數退避，最長 30 秒
- **Go**：`paho.mqtt.golang` 自動重連 `SetAutoReconnect(true)`
- 兩種發送器在指標不可用時靜默回傳零值（GPU 遺失不會當機）

## 相關頁面

- [系統架構](../../architecture.md) -- 發送器在系統中的位置
- [協定](protocol.md) -- Metrics v2 payload 格式
- [韌體](firmware.md) -- 韌體如何消費發送器資料
