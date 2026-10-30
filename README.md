# Lab 5: Smart Home with ESP RainMaker & Alexa

This project implements a complete Smart Home IoT solution using two ESP32 microcontrollers. It integrates with the ESP RainMaker cloud for remote monitoring and control, and Amazon Alexa for voice control.

## Architecture
- **Sensor Node (ESP32):** Reads temperature (DHT11) and ambient light (LDR). Displays data on an SSD1306 OLED. Pushes data to the RainMaker cloud.
- **Plug Node (ESP32):** Controls a relay module to power appliances. Features a physical push button for manual toggle.
- **Cloud:** ESP RainMaker handles device provisioning, MQTT communication, and mobile app syncing.
- **Voice Control:** Amazon Alexa skill linked via RainMaker for voice commands.

## Features
- [x] Wi-Fi Provisioning via BLE (Bluetooth Low Energy).
- [x] Real-time temperature and light intensity monitoring.
- [x] Remote relay control via the RainMaker mobile app.
- [x] Automation support (e.g., If Light < 10%, turn Plug ON).
- [x] Amazon Alexa voice integration.
- [x] Memory-optimized TLS connection (resolved `mbedtls` out-of-memory errors).
- [x] Manual hardware button override for the Plug.

## Hardware Requirements
- 2x ESP32 Development Boards
- 1x DHT11 Temperature Sensor
- 1x LDR (Light Dependent Resistor)
- 1x SSD1306 OLED Display (I2C)
- 1x 2-Channel 3.3V Relay Module (Active Low) *(Note: Only IN1 is used for this project)*
- 1x Push Button

## Pin Connections

### Sensor Node
- DHT11 Data: GPIO 15
- LDR Analog Out: GPIO 34
- OLED SDA: GPIO 21
- OLED SCL: GPIO 22

### Plug Node
- Relay IN1: GPIO 19 *(Only the first channel is used)*
- Push Button: GPIO 4

## How to Use
1. Flash `Lab5_Sensor` and `Lab5_Plug` using PlatformIO.
2. Open the Serial Monitor (115200 baud). Press the EN button to generate the QR code.
3. Scan the QR code using the **ESP RainMaker** mobile app to provision the devices to your 2.4GHz Wi-Fi network.
4. In the ESP RainMaker app, go to **Settings > Voice Services > Amazon Alexa** to link your account.
5. Open the **Amazon Alexa** app and say *"Discover my devices"*.
6. You can now control the plug using the app, Alexa voice commands, or the physical button. You can create automations in the RainMaker app to link the Sensor's light readings to the Plug's relay.
