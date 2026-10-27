#include <Arduino.h>
#include <RMaker.h>
#include <RMakerDevice.h>
#include <RMakerParam.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_netif.h>
#include <time.h>

// --- Pin Definitions ---
#define RELAY_PIN 19
#define BUTTON_PIN 4   

// --- RainMaker Variables ---
Device plugDevice("Plug");
Param powerParam("Power", "esp.param.power", value(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
bool currentState = false;
volatile bool timeSyncStarted = false;
volatile bool rainMakerStarted = false;
unsigned long timeSyncDeadline = 0;
bool networkReady = false;
bool timeSyncFailed = false;
unsigned long lastNtpRetry = 0;
IPAddress provisionedIp;
IPAddress provisionedGateway;

int buildMonth(const char *month) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; ++i) {
        if (strncmp(month, months[i], 3) == 0) return i;
    }
    return 0;
}

void setBuildTimeFallback() {
    char month[4] = {0};
    int day, year, hour, minute, second;
    sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    struct tm buildTime = {};
    buildTime.tm_year = year - 1900;
    buildTime.tm_mon = buildMonth(month);
    buildTime.tm_mday = day;
    buildTime.tm_hour = hour;
    buildTime.tm_min = minute;
    buildTime.tm_sec = second;

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t timestamp = mktime(&buildTime);
    struct timeval tv = {timestamp, 0};
    settimeofday(&tv, nullptr);
    Serial.println("NTP unavailable; using firmware build time for TLS");
}

void startRainMakerAfterTimeSync() {
    if (rainMakerStarted || !networkReady || !timeSyncStarted || WiFi.status() != WL_CONNECTED) return;

    time_t now = time(nullptr);
    if (now < 1704067200) {
        if (millis() >= timeSyncDeadline && !timeSyncFailed) {
            setBuildTimeFallback();
            Serial.println("Starting RainMaker with fallback clock...");
            RMaker.start();
            rainMakerStarted = true;
            timeSyncFailed = true;
        }
        if (!rainMakerStarted && timeSyncFailed && millis() - lastNtpRetry >= 30000) {
            lastNtpRetry = millis();
            timeSyncFailed = false;
            timeSyncDeadline = millis() + 30000;
            Serial.println("Retrying NTP synchronization...");
            configTime(0, 0, "216.239.35.0", "162.159.200.1", "129.6.15.28");
        }
        return;
    }

    Serial.println("NTP time synchronized");
    Serial.println("Starting RainMaker...");
    RMaker.start();
    rainMakerStarted = true;
}

void provisioningEvent(arduino_event_t *event) {
    switch (event->event_id) {
        case ARDUINO_EVENT_PROV_START:
            Serial.println("\nProvisioning started on BLE");
            WiFiProv.printQR("PROV_ESP32", "12345678", "ble");
            break;
        case ARDUINO_EVENT_PROV_CRED_RECV:
            Serial.println("Wi-Fi credentials received");
            break;
        case ARDUINO_EVENT_PROV_CRED_SUCCESS: {
            Serial.println("Wi-Fi provisioning succeeded");
            Serial.printf("Free heap after provisioning: %u bytes\n", ESP.getFreeHeap());
            
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_dns_info_t dns;
                dns.ip.type = ESP_IPADDR_TYPE_V4;
                dns.ip.u_addr.ip4.addr = static_cast<uint32_t>(IPAddress(8, 8, 8, 8));
                esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
                Serial.println("Manually forced DNS to 8.8.8.8");
            }
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("Connected to Wi-Fi");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.println("Wi-Fi received an IP address");
            provisionedIp = IPAddress(event->event_info.got_ip.ip_info.ip.addr);
            Serial.print("Event IP address: ");
            Serial.println(provisionedIp);
            provisionedGateway = IPAddress(event->event_info.got_ip.ip_info.gw.addr);
            Serial.print("Gateway: ");
            Serial.println(provisionedGateway);
            if (provisionedIp != IPAddress(0, 0, 0, 0)) {
                networkReady = true;
            }
            break;
        case ARDUINO_EVENT_PROV_CRED_FAIL:
            Serial.println("Wi-Fi provisioning failed; check the 2.4 GHz network and password");
            break;
        default:
            break;
    }
}

// The callback signature for the current Arduino core
void write_cb(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx) {
    const char *param_name = param->getParamName();
    if (strcmp(param_name, "Power") == 0) {
        currentState = val.val.b;
        
        // INVERTED LOGIC: LOW is ON, HIGH is OFF for active-low relay modules
        digitalWrite(RELAY_PIN, currentState ? LOW : HIGH); 
        
        Serial.printf("Cloud command: Relay set to %s\n", currentState ? "ON" : "OFF");
        param->updateAndReport(val);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    
    // INVERTED LOGIC: Start HIGH so the relay is OFF on boot
    digitalWrite(RELAY_PIN, HIGH); 
    
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

    // --- SKIP PROVISIONING IF ALREADY CONNECTED ---
    WiFi.mode(WIFI_STA);
    WiFi.begin(); 

    Serial.println("Checking for saved Wi-Fi credentials...");
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) { // Wait up to 10 seconds
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nAlready provisioned! Connected to Wi-Fi.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        networkReady = true; // Set this so RainMaker starts in the loop
    } else {
        Serial.println("\nNo Wi-Fi credentials found. Starting BLE Provisioning...");
        WiFi.onEvent(provisioningEvent);
        WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_NONE,
                                WIFI_PROV_SECURITY_1, "12345678", "PROV_ESP32",
                                nullptr, nullptr, true);
    }
}

void loop() {
    if (!networkReady && WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        provisionedIp = WiFi.localIP();
        networkReady = true;
    }

    if (networkReady && !timeSyncStarted) {
        Serial.print("IP address: ");
        Serial.println(provisionedIp);
        Serial.print("Gateway: ");
        Serial.println(provisionedGateway);
        Serial.println("Starting NTP time synchronization...");
        configTime(0, 0, "216.239.35.0", "162.159.200.1", "129.6.15.28");
        timeSyncStarted = true;
        timeSyncDeadline = millis() + 30000;
    }

    startRainMakerAfterTimeSync();

    // Read physical button to toggle relay manually
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(50); // debounce
        if (digitalRead(BUTTON_PIN) == LOW) {
            currentState = !currentState;
            
            // INVERTED LOGIC HERE TOO
            digitalWrite(RELAY_PIN, currentState ? LOW : HIGH); 
            
            param_val_t val = value(currentState);
            powerParam.updateAndReport(val); 
            Serial.printf("Button pressed: Relay toggled %s\n", currentState ? "ON" : "OFF");
            while(digitalRead(BUTTON_PIN) == LOW); // Wait for release
        }
    }
    delay(10);
}