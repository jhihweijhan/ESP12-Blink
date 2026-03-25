# Sender Module

The sender collects host system metrics and publishes them to an MQTT broker. Two implementations exist: Python (cross-platform, Docker-ready) and Go (single static binary).

## Data Collection Chain

```
OS Metrics --> Collector (psutil/gopsutil) --> buildPayload() --> MQTT Publish
```

### Python Sender

Entry point: `apps/sender/python/sender_v2.py`

```python
# Simplified data flow
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

Key files:

| File | Purpose |
|------|---------|
| `sender_v2.py` | Main loop: connect MQTT, collect metrics, publish at interval |
| `metrics_payload.py` | `build_payload()`: assemble compact JSON with positional arrays |

### Go Sender

Entry point: `apps/sender/go/main.go`

```go
// Simplified data flow
payload := buildPayload(hostname)
token := client.Publish(
    fmt.Sprintf("sys/agents/%s/metrics/v2", hostname),
    qos, false, payload,
)
```

Key files:

| File | Purpose |
|------|---------|
| `main.go` | Main loop: MQTT client setup, metric collection, publish loop |
| `payload.go` | `buildPayload()`: assemble `Payload` struct with JSON tags |

## GPU Detection Strategy

Both senders use a cascading detection approach:

```
1. NVIDIA GPU?  --> nvidia-smi CLI --> parse XML/CSV output
2. AMD GPU?     --> rocm-smi CLI   --> parse output
3. Linux sysfs? --> /sys/class/drm/card*/device/ --> read hwmon temps
4. None found   --> return zeros (gpu: [0, 0, 0, 0, 0])
```

The cascade runs at each collection interval, allowing hot-plug GPU detection.

## Environment Configuration

All configuration is via environment variables (no config file needed):

| Variable | Default | Description |
|----------|---------|-------------|
| `MQTT_HOST` | `127.0.0.1` | MQTT broker hostname |
| `MQTT_PORT` | `1883` | MQTT broker port |
| `MQTT_USER` | (empty) | Broker authentication username |
| `MQTT_PASS` | (empty) | Broker authentication password |
| `MQTT_QOS` | `0` | MQTT QoS level (0, 1, or 2) |
| `SENDER_HOSTNAME` | system hostname | Override reported hostname |
| `SEND_INTERVAL_SEC` | `1.0` | Publish frequency in seconds |

## Deployment Options

### Docker Compose (Python)

```bash
cd apps/sender/python
./senderctl.sh quickstart-compose
```

The `senderctl.sh` script provides:

- `quickstart-compose` -- Interactive setup wizard + start
- `compose-status` -- Check container status
- `compose-logs` -- View sender logs
- `compose-restart` -- Restart the sender
- `ensure-docker-boot` -- Enable auto-start on reboot

### Static Binary (Go)

```bash
cd apps/sender/go
go build -o sender-go .
MQTT_HOST=192.168.1.100 ./sender-go
```

The Go sender compiles to a single static binary with no external dependencies.

### systemd Service

For persistent deployment without Docker:

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

## Error Handling

- **Python**: `connect_mqtt_with_retry()` with exponential backoff up to 30s
- **Go**: `paho.mqtt.golang` auto-reconnect via `SetAutoReconnect(true)`
- Both senders silently return zero values for unavailable metrics (no crash on missing GPU)

## Related Pages

- [Architecture](../architecture.md) -- Where the sender fits in the system
- [Protocol](protocol.md) -- Metrics v2 payload format
- [Firmware](firmware.md) -- How the firmware consumes sender data
