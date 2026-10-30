#include <Arduino.h>
#include <RMaker.h>
#include <RMakerDevice.h>
#include <RMakerParam.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// --- Pin Definitions ---
#define DHTPIN 15
#define DHTTYPE DHT11
#define LDR_PIN 34
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- RainMaker Variables ---
Device sensorDevice("Sensor", "esp.device.temperature-sensor");
Param tempParam("Temperature", "esp.param.temperature", value(0.0f), PROP_FLAG_READ);
Param lightParam("Light", "esp.param.light", value(0), PROP_FLAG_READ);

void provisioningEvent(arduino_event_t *event) {
    switch (event->event_id) {
        case ARDUINO_EVENT_PROV_START:
            Serial.println("\nProvisioning started on BLE");
            WiFiProv.printQR("PROV_SENSOR", "12345678", "ble");
            break;
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    dht.begin();
    pinMode(LDR_PIN, INPUT);

    // Initialize OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("Starting...");
    display.display();

    // Initialize RainMaker Node
    Node my_node = RMaker.initNode("ESP32_Sensor");
    if (!my_node.getNodeHandle()) {
        Serial.println("RainMaker storage failed. Erase the ESP32 flash, then upload again.");
        return;
    }
    
    // Add parameters
    lightParam.addBounds(value(0), value(100), value(1)); 
    lightParam.addUIType(ESP_RMAKER_UI_SLIDER);
    sensorDevice.addParam(tempParam);
    sensorDevice.addParam(lightParam);
    sensorDevice.addNameParam();
    my_node.addDevice(sensorDevice);

    // --- THE FIX TO SHOW THE QR CODE ---
    RMaker.start();

    WiFi.onEvent(provisioningEvent);
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, 
                            WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                            WIFI_PROV_SECURITY_1, "12345678", "PROV_SENSOR");
}

void loop() {
    // Read Sensors and Send ONLY on Change
    static unsigned long lastUpdate = 0;
    static int lastLight = -1;
    static float lastTemp = -1.0;
    
    if (millis() - lastUpdate > 5000) {
        lastUpdate = millis();
        
        float t = dht.readTemperature();
        
        if (isnan(t)) {
            Serial.println("Failed to read DHT11");
            return; 
        }
        
        int lightState = digitalRead(LDR_PIN);
        int lightPercent = (lightState == LOW) ? 100 : 0; 

        if (lightPercent != lastLight || t != lastTemp) {
            tempParam.updateAndReport(value((float)t));
            lightParam.updateAndReport(value((int)lightPercent));
            lastLight = lightPercent;
            lastTemp = t;
            Serial.printf("Sent - Temp: %.2f, Light: %d%%\n", t, lightPercent);
        } else {
            Serial.printf("No change - Temp: %.2f, Light: %d%% (Not sent)\n", t, lightPercent);
        }
        
        display.clearDisplay();
        display.setCursor(0,0);
        display.print("Temp: "); display.print(t); display.println(" C");
        display.print("Light: "); display.print(lightPercent); display.println(" %");
        display.print("Wi-Fi: "); display.println(WiFi.status() == WL_CONNECTED ? "OK" : "LOST");
        display.display();
    }
    
    delay(10);
}