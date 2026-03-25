# Mochi-Metrics

**IoT System Monitoring with ESP8266 TFT Display**

Mochi-Metrics is an IoT system monitoring solution: desktop senders (Python/Go) collect CPU, RAM, GPU, network, and disk metrics, then publish them via MQTT to an ESP8266 firmware that parses and renders the data on a 240x240 TFT display.

## System Overview

```
Host Metrics --> Python/Go Sender --> MQTT Broker --> ESP8266 Firmware --> TFT Display
```

- **Senders** collect system metrics using `psutil` (Python) or `gopsutil` (Go) and publish compact JSON payloads at 1Hz
- **MQTT Broker** routes messages using topic pattern `sys/agents/{hostname}/metrics/v2`
- **ESP8266 Firmware** subscribes to MQTT topics, parses metrics, and drives the TFT display with dirty-region optimization
- **Web UI** on the ESP8266 provides configuration via REST API (ESPAsyncWebServer)

## Quick Links

- [Architecture](architecture.md) -- Full system data flow diagram with Mermaid
- [Learning Path](learning-path.md) -- Three learning tracks covering embedded IoT, networking, and system monitoring
- [Modules](modules/firmware.md) -- Deep-dive into each component

## Getting Started

See the [Installation Guide](https://github.com/jhihweijhan/Mochi-Metrics) for setup instructions.
