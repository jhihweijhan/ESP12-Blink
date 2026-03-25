# Protocol Module

The Metrics v2 protocol defines the MQTT topic format and JSON payload structure used for communication between senders and the ESP8266 firmware.

## Topic Format

```
sys/agents/{hostname}/metrics/v2
```

- `{hostname}` is the sender's machine hostname (or `SENDER_HOSTNAME` override)
- Firmware extracts the hostname from the topic path (not the payload) for trust boundary
- Wildcard subscription: `sys/agents/+/metrics/v2` for automatic device discovery

## Payload Structure

Compact JSON with fixed keys and positional arrays:

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

## Field Reference

### Top-Level Fields

| Key | Type | Description |
|-----|------|-------------|
| `v` | int | Schema version, must be `2` |
| `ts` | int | Sender Unix epoch in milliseconds |
| `h` | string | Sender hostname |

### CPU Array

`cpu: [cpu_percent, cpu_temp_c]`

| Index | Type | Unit | Description |
|-------|------|------|-------------|
| 0 | float | % | CPU usage percentage (0-100) |
| 1 | float | C | CPU temperature in Celsius |

### RAM Array

`ram: [ram_percent, ram_used_mb, ram_total_mb]`

| Index | Type | Unit | Description |
|-------|------|------|-------------|
| 0 | float | % | RAM usage percentage (0-100) |
| 1 | int | MB | RAM currently used |
| 2 | int | MB | Total RAM installed |

### GPU Array

`gpu: [gpu_percent, gpu_temp_c, gpu_mem_percent, gpu_hotspot_c, gpu_mem_temp_c]`

| Index | Type | Unit | Description |
|-------|------|------|-------------|
| 0 | float | % | GPU usage percentage (0-100) |
| 1 | float | C | GPU core temperature |
| 2 | float | % | GPU memory usage percentage |
| 3 | float | C | GPU hotspot temperature (0 if unavailable) |
| 4 | float | C | GPU memory temperature (0 if unavailable) |

### Network Array

`net: [rx_kbps, tx_kbps]`

| Index | Type | Unit | Description |
|-------|------|------|-------------|
| 0 | int | kbps | Network receive rate |
| 1 | int | kbps | Network transmit rate |

### Disk Array

`disk: [read_kBps, write_kBps]`

| Index | Type | Unit | Description |
|-------|------|------|-------------|
| 0 | int | kB/s | Disk read rate |
| 1 | int | kB/s | Disk write rate |

## x10 Fixed-Point Encoding (Firmware Side)

The ESP8266 firmware stores float values as `int16_t` scaled by 10 to avoid floating-point operations:

| Sender Value | Firmware Storage | Display Value |
|-------------|-----------------|---------------|
| `42.4` (CPU %) | `424` (int16_t) | `42%` |
| `58.2` (CPU temp) | `582` (int16_t) | `58C` |
| `67.8` (RAM %) | `678` (int16_t) | `68%` |

Conversion helpers in `metrics_v2.h`:

- `roundedPercent(424)` returns `42`
- `roundedTempC(582)` returns `58`

## Rules

1. Firmware only accepts topics matching `/metrics/v2`
2. Firmware rejects payloads where `v != 2`
3. Recommended sender frequency: 1Hz per host
4. Sender should always send all arrays; use `0` for unavailable values
5. Maximum payload size: 1024 bytes (`MQTT_MAX_PAYLOAD_BYTES`)
6. Maximum monitored devices: 8 (`MAX_DEVICES`)

## Related Pages

- [Architecture](../architecture.md) -- Full system data flow
- [Firmware](firmware.md) -- How firmware parses and displays metrics
- [Sender](sender.md) -- How senders collect and publish metrics
