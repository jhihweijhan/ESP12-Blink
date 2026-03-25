# Firmware Module

The ESP8266 firmware is the core of Mochi-Metrics: it receives MQTT metrics, stores per-device data, and renders it on a 240x240 TFT display using a cooperative single-threaded event loop.

## File Structure

All firmware modules are header files in `apps/firmware/src/include/` (single translation unit):

| File | Role |
|------|------|
| `main.cpp` | Entry point: `setup()` + `loop()` state machine |
| `mqtt_transport.h` | MQTT connection lifecycle, subscription, message dispatch |
| `metrics_parser_v2.h` | JSON parsing into `MetricsFrameV2` struct |
| `device_store.h` | Per-device metrics storage with dirty-mask tracking |
| `monitor_display.h` | TFT rendering with dirty-region optimization |
| `monitor_config.h` | LittleFS-based persistent configuration |
| `web_server.h` | ESPAsyncWebServer REST API |
| `tft_driver.h` | SPI display driver (ST7789, batch transfer) |
| `ui_components.h` | Reusable UI drawing primitives |
| `metrics_v2.h` | `MetricsFrameV2` data structure definition |
| `connection_policy.h` | Pure-logic decision functions (testable on native) |

## Core Concepts

### Cooperative Loop

The ESP8266 runs a single-threaded `loop()` function. All operations must be non-blocking to keep the UI responsive:

```cpp
void loop() {
    mqttTransport.loop();       // Drive MQTT client
    webServer.loop();           // Handle HTTP requests
    monitorDisplay.loop();      // Update TFT display
    configManager.loop();       // Deferred config save (5s debounce)
}
```

If any operation blocks (e.g., synchronous TCP connect), the entire UI freezes until it completes.

### Dirty-Region Display

Instead of redrawing the entire screen every frame, the firmware tracks which metric groups changed:

1. `DeviceStore::updateFrame()` compares old vs. new frame field-by-field
2. Sets dirty bits: `DIRTY_CPU`, `DIRTY_RAM`, `DIRTY_GPU`, `DIRTY_NET`, `DIRTY_DISK`, `DIRTY_ONLINE`
3. `MonitorDisplay` only redraws rows whose dirty bits are set

Three refresh tiers optimize responsiveness:

| Tier | Interval | When |
|------|----------|------|
| Force redraw | 90ms | Screen state change (device switch) |
| Active update | 200ms | New data arriving |
| Idle refresh | 1000ms | No recent data |

### x10 Fixed-Point Integers

`MetricsFrameV2` stores percentages and temperatures as `int16_t` scaled by 10 to avoid floating-point operations on the ESP8266:

```cpp
// 42.3% CPU usage stored as 423
int16_t cpuPercent;  // x10 scaled

// Conversion helpers in metrics_v2.h
int roundedPercent(int16_t x10val);  // 423 -> 42
int roundedTempC(int16_t x10val);    // 582 -> 58
```

### SPI Batch Transfer

The TFT driver uses batch SPI transfers instead of per-pixel writes:

- **64-byte SPI buffer** matches ESP8266 FIFO capacity
- `drawCharScaled()` uses row-batch rendering (up to size 4)
- 40MHz SPI clock (ESP8266 80/2 divider, within ST7789 spec)

## Key Design Decisions

- **Header-only architecture**: All `.h` files compile in a single translation unit. Simplifies PlatformIO builds but increases rebuild time.
- **Testable policy layer**: `connection_policy.h` in `include/` (not `src/include/`) contains pure logic functions testable on native platforms without Arduino dependencies.
- **espMqttClientAsync with keepalive=0**: PINGREQ is unreliable on ESP8266; firmware uses a custom 30-second silence check instead.

## Related Pages

- [Architecture](../architecture.md) -- System-level data flow
- [Protocol](protocol.md) -- Metrics v2 JSON specification
- [Sender](sender.md) -- Data source for the firmware
