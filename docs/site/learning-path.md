# Learning Path

This guide presents three learning tracks through Mochi-Metrics. Each track highlights specific technologies and concepts you can learn by studying the corresponding modules.

## Recommended Order

Start with the **Protocol spec** to understand the data format, then follow any track that interests you.

```
Protocol Spec --> Sender --> MQTT --> Firmware --> Display
```

## Learning Tracks

```mermaid
graph TD
    Start["Mochi-Metrics<br/>Learning Tracks"]

    Start --> IoT["Embedded IoT Path"]
    Start --> Net["Networking Path"]
    Start --> Sys["System Monitoring Path"]

    subgraph Embedded["Embedded IoT Path"]
        SPI["SPI Protocol<br/>- Batch transfer<br/>- Clock tuning (40MHz)<br/>- PROGMEM usage"]
        TFT["TFT Rendering<br/>- ST7789 driver<br/>- RGB565 color encoding<br/>- Dirty-region redraw"]
        Mem["ESP8266 Memory<br/>- 80KB RAM constraints<br/>- Heap fragmentation<br/>- LittleFS persistence"]
        Loop["Cooperative Loop<br/>- Non-blocking design<br/>- State machines<br/>- Timer-based scheduling"]
        SPI --> TFT --> Mem --> Loop
    end

    subgraph Network["Networking Path"]
        MQTT["MQTT Pub/Sub<br/>- Topic patterns<br/>- QoS levels<br/>- Wildcard subscription"]
        Async["Async Reconnect<br/>- espMqttClientAsync<br/>- Backoff strategy<br/>- Silence detection"]
        WiFi["WiFi AP/STA<br/>- Captive portal<br/>- QR code pairing<br/>- Multi-phase retry"]
        WebAPI["Web Server<br/>- ESPAsyncWebServer<br/>- REST API design<br/>- JSON config endpoints"]
        MQTT --> Async --> WiFi --> WebAPI
    end

    subgraph SysMon["System Monitoring Path"]
        Metrics["OS Metrics Collection<br/>- psutil (Python)<br/>- gopsutil (Go)<br/>- CPU, RAM, GPU, Net, Disk"]
        GPU["GPU Detection<br/>- nvidia-smi<br/>- rocm-smi<br/>- Linux sysfs hwmon"]
        JSON["JSON Serialization<br/>- Compact payloads<br/>- Positional arrays<br/>- x10 fixed-point encoding"]
        Deploy["Cross-platform Deploy<br/>- Go static binary<br/>- Docker Compose<br/>- systemd service<br/>- env-based config"]
        Metrics --> GPU --> JSON --> Deploy
    end

    IoT --> SPI
    Net --> MQTT
    Sys --> Metrics
```

## Track Details

### Embedded IoT Path

Best for learners interested in microcontroller programming and display systems.

| Module | Key Technologies | Source Files |
|--------|-----------------|-------------|
| SPI Protocol | Batch SPI transfer, 40MHz clock, FIFO buffer (64 bytes) | `tft_driver.h` |
| TFT Rendering | ST7789 driver, RGB565 color, `drawCharScaled()` row-batch | `tft_driver.h`, `ui_components.h` |
| ESP8266 Memory | Heap monitoring, `ESP.getFreeHeap()`, LittleFS config | `monitor_config.h` |
| Cooperative Loop | Arduino `loop()`, non-blocking state machine, timer scheduling | `main.cpp` |

### Networking Path

Best for learners interested in IoT communication protocols and web services.

| Module | Key Technologies | Source Files |
|--------|-----------------|-------------|
| MQTT Pub/Sub | Topic hierarchy, wildcard `+`, QoS 0/1/2 | `mqtt_transport.h` |
| Async Reconnect | espMqttClientAsync, 1-5s backoff, keepalive=0, 30s silence check | `mqtt_transport.h`, `connection_policy.h` |
| WiFi Management | AP mode captive portal, STA multi-phase retry, QR code setup | `wifi_manager.h` |
| Web Server | ESPAsyncWebServer, REST JSON API, async request handling | `web_server.h` |

### System Monitoring Path

Best for learners interested in system metrics and cross-platform deployment.

| Module | Key Technologies | Source Files |
|--------|-----------------|-------------|
| OS Metrics | `psutil` / `gopsutil`, CPU/RAM/disk/network sampling | `sender_v2.py`, `main.go` |
| GPU Detection | nvidia-smi CLI, rocm-smi CLI, Linux sysfs `/sys/class/drm/` | `metrics_payload.py`, `payload.go` |
| JSON Serialization | Compact positional arrays, schema versioning (`v: 2`) | `metrics_payload.py`, `payload.go` |
| Deployment | Go cross-compile, Docker Compose, `senderctl.sh`, env config | `main.go`, `docker-compose.yml` |

## Next Steps

- [Architecture](architecture.md) -- Understand the full system data flow
- [Firmware Module](modules/firmware.md) -- Deep-dive into ESP8266 firmware
- [Sender Module](modules/sender.md) -- Explore metrics collection
- [Protocol Module](modules/protocol.md) -- Learn the Metrics v2 specification
