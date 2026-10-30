#include <Arduino.h>
#include <RMaker.h>
#include <RMakerDevice.h>
#include <RMakerParam.h>
#include <WiFi.h>
#include <WiFiProv.h>

// --- Pin Definitions ---
#define RELAY_PIN 19
#define BUTTON_PIN 4   

// --- RainMaker Variables ---
Device plugDevice("Plug", "esp.device.switch");
Param powerParam("Power", "esp.param.power", value(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
bool currentState = false;

void provisioningEvent(arduino_event_t *event) {
    switch (event->event_id) {
        case ARDUINO_EVENT_PROV_START:
            Serial.println("\nProvisioning started on BLE");
            WiFiProv.printQR("PROV_ESP32", "12345678", "ble");
            break;
        case ARDUINO_EVENT_PROV_INIT:
            // Do not let the provisioning manager allocate its automatic
            // stop task while RainMaker is using the remaining heap.
            wifi_prov_mgr_disable_auto_stop(10000);
            break;
        case ARDUINO_EVENT_PROV_CRED_SUCCESS:
            Serial.println("Wi-Fi credentials received; stopping provisioning");
            wifi_prov_mgr_stop_provisioning();
            break;
        case ARDUINO_EVENT_PROV_CRED_FAIL:
            Serial.println("Wi-Fi provisioning failed; check the 2.4 GHz network and password");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("Connected to Wi-Fi. IP: ");
            Serial.println(WiFi.localIP());
            break;
        default:
            break;
    }
}

// Callback for Alexa/App commands
void write_cb(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx) {
    const char *param_name = param->getParamName();
    if (strcmp(param_name, "Power") == 0) {
        currentState = val.val.b;
        
        // INVERTED LOGIC: LOW is ON, HIGH is OFF
        digitalWrite(RELAY_PIN, currentState ? LOW : HIGH); 
        
        Serial.printf("Cloud command: Relay set to %s\n", currentState ? "ON" : "OFF");
        param->updateAndReport(val);
    }
}

void setup() {
    Serial.begin(115200);
    
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); // Start OFF
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Initialize RainMaker Node
    Node my_node = RMaker.initNode("ESP32_Plug");
    if (!my_node.getNodeHandle()) {
        Serial.println("RainMaker storage failed. Erase the ESP32 flash, then upload again.");
        return;
    }
    
    // Add parameters
    powerParam.addUIType(ESP_RMAKER_UI_TOGGLE);
    plugDevice.addCb(write_cb);
    plugDevice.addParam(powerParam);
    plugDevice.addNameParam();
    my_node.addDevice(plugDevice);

    // --- EXACT SENSOR LOGIC ---
    // 1. Start RainMaker first (Prevents "Setting up node" hang)
    RMaker.start();

    // 2. Start provisioning with BTDM memory free (Prevents SSL crash)
    WiFi.onEvent(provisioningEvent);
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, 
                            WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                            WIFI_PROV_SECURITY_1, "12345678", "PROV_ESP32");
}

void loop() {
    // Read physical button to toggle relay manually
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(50); // debounce
        if (digitalRead(BUTTON_PIN) == LOW) {
            currentState = !currentState;
            digitalWrite(RELAY_PIN, currentState ? LOW : HIGH); 
            
            param_val_t val = value(currentState);
            powerParam.updateAndReport(val); 
            Serial.printf("Button pressed: Relay toggled %s\n", currentState ? "ON" : "OFF");
            while(digitalRead(BUTTON_PIN) == LOW); // Wait for release
        }
    }
    delay(10);
}