# Lab 5: Smart Home with ESP RainMaker & Amazon Alexa

A complete Smart Home IoT solution built on two ESP32 microcontrollers, developed **entirely with the ESP-IDF framework** (no Arduino core). The system uses the **ESP RainMaker** cloud for remote monitoring, control, and automation, and **Amazon Alexa** for voice control.

![ESP-IDF](https://img.shields.io/badge/framework-ESP--IDF-red)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Cloud](https://img.shields.io/badge/cloud-ESP%20RainMaker-green)
![Voice](https://img.shields.io/badge/voice-Amazon%20Alexa-lightblue)

---

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [System Architecture](#system-architecture)
4. [Hardware Requirements](#hardware-requirements)
5. [Pin Connections](#pin-connections)
6. [Software Requirements](#software-requirements)
7. [Project Structure](#project-structure)
8. [Build & Flash](#build--flash)
9. [Provisioning & Setup](#provisioning--setup)
10. [Alexa Integration](#alexa-integration)
11. [Automation Example](#automation-example)
12. [Memory Optimization](#memory-optimization)
13. [Troubleshooting](#troubleshooting)

---

## Overview

The project is split into two independent nodes that communicate through the ESP RainMaker cloud:

| Node | Role | Key Peripherals |
|------|------|-----------------|
| **Sensor Node** | Measures temperature and ambient light, shows readings locally, and reports them to the cloud | DHT11, LDR, SSD1306 OLED |
| **Plug Node** | Switches a mains appliance through a relay, controllable from the cloud, Alexa, or a physical button | Relay module, push button |

## Features

- **BLE Wi-Fi provisioning** via QR code and the ESP RainMaker mobile app
- **Real-time monitoring** of temperature and light intensity
- **Local display** of sensor data on an SSD1306 OLED
- **Remote relay control** through the RainMaker mobile app
- **Voice control** with Amazon Alexa
- **Cloud automations** (e.g. *If Light < 10 %, turn Plug ON*)
- **Manual override** using a physical push button on the Plug node
- **Memory-optimized TLS** connection (resolves `mbedtls` out-of-memory errors)

## System Architecture

```
┌──────────────────┐                                  ┌──────────────────┐
│   Sensor Node    │                                  │    Plug Node     │
│     (ESP32)      │                                  │     (ESP32)      │
│  DHT11 · LDR     │                                  │  Relay · Button  │
│  SSD1306 OLED    │                                  │                  │
└────────┬─────────┘                                  └────────┬─────────┘
         │  MQTT / TLS                        MQTT / TLS        │
         └─────────────────►  ESP RainMaker Cloud  ◄────────────┘
                                   │         │
                       ┌───────────┘         └───────────┐
                       ▼                                 ▼
              RainMaker Mobile App               Amazon Alexa Skill
          (monitor · control · automate)          (voice commands)
```

- **Sensor Node (ESP32):** Reads temperature (DHT11) and ambient light (LDR), displays them on the OLED, and pushes values to the RainMaker cloud.
- **Plug Node (ESP32):** Drives a relay to power appliances; a push button toggles the state manually and keeps the cloud state in sync.
- **Cloud:** ESP RainMaker handles device provisioning, MQTT communication, user/device management, and mobile app synchronization.
- **Voice Control:** An Amazon Alexa skill, linked through RainMaker, exposes the Plug as a controllable smart-home device.

## Hardware Requirements

| Qty | Component | Notes |
|-----|-----------|-------|
| 2 | ESP32 development board | One per node |
| 1 | DHT11 temperature sensor | Sensor Node |
| 1 | LDR (light dependent resistor) | Sensor Node; requires a voltage divider |
| 1 | SSD1306 OLED display (I2C) | Sensor Node |
| 1 | 2-channel 3.3 V relay module (**active low**) | Plug Node; only **IN1** is used |
| 1 | Push button | Plug Node |

> **Note:** The relay module is active-low: driving the input pin **LOW** energizes the relay.

## Pin Connections

### Sensor Node

| Peripheral | Signal | ESP32 GPIO |
|------------|--------|-----------|
| DHT11 | Data | GPIO 4 |
| LDR | Analog out | GPIO 34 (ADC1, input only) |
| SSD1306 OLED | SDA | GPIO 21 |
| SSD1306 OLED | SCL | GPIO 22 |

### Plug Node

| Peripheral | Signal | ESP32 GPIO |
|------------|--------|-----------|
| Relay module | IN1 (channel 1 only) | GPIO 19 |
| Push button | Input | GPIO 4 |

> **Wiring tips**
> - Build the LDR as a voltage divider (LDR + fixed resistor, typically ~10 kΩ) so GPIO 34 sees a usable analog voltage.
> - The DHT11 data line needs a pull-up resistor (~4.7–10 kΩ) unless the breakout board already includes one.
> - Use an internal pull-up (or external resistor) for the push button and wire it between the GPIO and GND.
> - Take care when switching mains voltage. Use properly rated, insulated relay wiring.

## Software Requirements

- **ESP-IDF** (v5.x recommended) with the **ESP RainMaker** component (`esp-rainmaker`)
- **PlatformIO** configured with `framework = espidf`
- ESP RainMaker mobile app (iOS / Android)
- Amazon Alexa mobile app and an Amazon account
- Serial terminal at **115200 baud**

## Project Structure

```
Lab5-SmartHome/
├── Lab5_Sensor/                # Sensor Node firmware (ESP-IDF)
│   ├── .vscode/                # VS Code / PlatformIO settings
│   ├── certs/                  # Certificates used for the cloud (TLS) connection
│   ├── include/
│   ├── lib/
│   ├── managed_components/     # Components fetched by the IDF Component Manager
│   ├── src/
│   │   ├── CMakeLists.txt
│   │   └── main.c              # Sensor application (DHT11, LDR, OLED, RainMaker)
│   ├── test/
│   ├── CMakeLists.txt
│   ├── dependencies.lock       # Locked component versions
│   ├── partitions.csv          # Custom partition table
│   ├── platformio.ini          # PlatformIO config (framework = espidf)
│   ├── sdkconfig.defaults      # Default ESP-IDF configuration
│   └── sdkconfig.esp32dev      # Generated config for the esp32dev environment
│
└── Lab5_Plug/                  # Plug Node firmware (ESP-IDF)
    ├── .vscode/
    ├── include/
    ├── lib/
    ├── src/
    │   ├── CMakeLists.txt
    │   ├── idf_component.yml   # Component dependencies (e.g. RainMaker)
    │   └── main.c              # Plug application (relay, button, RainMaker)
    ├── test/
    ├── CMakeLists.txt
    ├── dependencies.lock
    ├── partitions.csv
    ├── platformio.ini
    ├── sdkconfig.defaults
    └── sdkconfig.esp32dev
```

Key files in each node:

| File | Purpose |
|------|---------|
| `src/main.c` | Application entry point (`app_main`) |
| `src/idf_component.yml` | Declares dependencies pulled in by the IDF Component Manager |
| `partitions.csv` | Partition table (app, NVS, and RainMaker factory/data partitions) |
| `sdkconfig.defaults` | Project configuration, including the `mbedtls` memory tuning |
| `dependencies.lock` | Pins exact component versions for reproducible builds |

## Build & Flash

1. Clone the repository and open it in VS Code with the PlatformIO extension.
2. Build and flash each node using the ESP-IDF framework:

   ```bash
   # Sensor Node
   cd Lab5_Sensor
   pio run -t upload

   # Plug Node
   cd ../Lab5_Plug
   pio run -t upload
   ```

3. Open the serial monitor at 115200 baud:

   ```bash
   pio device monitor -b 115200
   ```

## Provisioning & Setup

1. With the serial monitor open, press the **EN** (reset) button on the board. A **QR code** is printed to the console.
2. In the **ESP RainMaker** app, add a new device and **scan the QR code**.
3. Provision the device over **BLE** to your **2.4 GHz** Wi-Fi network (ESP32 does not support 5 GHz).
4. Repeat for the second node.

Once provisioned, both devices appear in the RainMaker app.

## Alexa Integration

1. In the ESP RainMaker app, go to **Settings → Voice Services → Amazon Alexa** and link your Amazon account.
2. Open the **Amazon Alexa** app and say: *"Alexa, discover my devices."*
3. Control the Plug by voice, e.g. *"Alexa, turn on the plug."*

The Plug can be controlled in three ways, all kept in sync with the cloud:

| Method | Description |
|--------|-------------|
| RainMaker app | Toggle remotely from anywhere |
| Alexa | Voice commands |
| Physical button | Local manual override |

## Automation Example

Use the RainMaker app to link the Sensor Node to the Plug Node:

- **Trigger:** Sensor light intensity `< 10 %`
- **Action:** Plug power `ON`

Similar rules can be created for temperature thresholds.

## Memory Optimization

Running TLS/MQTT, BLE provisioning, and RainMaker together on the ESP32 is RAM-intensive, which can trigger `mbedtls` allocation failures. This project addresses it by tuning the ESP-IDF configuration (`sdkconfig`), for example:

- Reducing the `mbedtls` buffer sizes and enabling dynamic buffer allocation
- Releasing BLE memory after provisioning completes
- Keeping task stack sizes and heap usage to what each node actually needs

Refer to each node's `sdkconfig.defaults` for the exact values used.

## Troubleshooting

| Symptom | Possible Cause / Fix |
|---------|----------------------|
| No QR code in the serial monitor | Press **EN** after opening the monitor; check the 115200 baud setting |
| Provisioning fails | Ensure the network is **2.4 GHz** and the password is correct |
| `mbedtls` out-of-memory errors | Verify the memory-optimization settings in `sdkconfig.defaults` |
| Alexa does not find the device | Re-link the skill in RainMaker, then say "discover my devices" again |
| Relay behaves inverted | The module is **active low**; confirm the GPIO logic level in firmware |
| OLED stays blank | Check SDA/SCL wiring (GPIO 21/22) and the I2C address |

---

**Course Lab 5** · Built with ESP-IDF · ESP RainMaker · Amazon Alexa
