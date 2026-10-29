#include <Arduino.h>
#include <RMaker.h>
#include <RMakerDevice.h>
#include <RMakerParam.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_netif.h>
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
Device sensorDevice("Sensor");
// FIXED: Added 'f' to 0.0 to make it a float, not a double
Param tempParam("Temperature", "esp.param.temperature", value(0.0f), PROP_FLAG_READ | PROP_FLAG_WRITE);
Param lightParam("Light", "esp.param.light", value(0), PROP_FLAG_READ | PROP_FLAG_WRITE);

volatile bool timeSyncStarted = false;
volatile bool rainMakerStarted = false;
unsigned long timeSyncDeadline = 0;
bool networkReady = false;
bool timeSyncFailed = false;
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
        return;
    }

    Serial.println("NTP time synchronized. Starting RainMaker...");
    RMaker.start();
    rainMakerStarted = true;
}

void provisioningEvent(arduino_event_t *event) {
    switch (event->event_id) {
        case ARDUINO_EVENT_PROV_START:
            Serial.println("\nProvisioning started on BLE");
            WiFiProv.printQR("PROV_SENSOR", "12345678", "ble");
            break;
        case ARDUINO_EVENT_PROV_CRED_RECV:
            Serial.println("Wi-Fi credentials received");
            break;
        case ARDUINO_EVENT_PROV_CRED_SUCCESS: {
            Serial.println("Wi-Fi provisioning succeeded");
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
            provisionedIp = IPAddress(event->event_info.got_ip.ip_info.ip.addr);
            provisionedGateway = IPAddress(event->event_info.got_ip.ip_info.gw.addr);
            networkReady = true;
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

    // Initialize RainMaker
    Node my_node = RMaker.initNode("ESP32_Sensor");
    if (!my_node.getNodeHandle()) {
        Serial.println("RainMaker storage failed. Erase flash.");
        return;
    }
    
    sensorDevice.addParam(tempParam);
    sensorDevice.addParam(lightParam);
    sensorDevice.addNameParam();
    my_node.addDevice(sensorDevice);

    // Expose reboot, Wi-Fi reset, and factory reset controls to RainMaker.
    esp_err_t systemServiceResult = RMaker.enableSystemService(SYSTEM_SERV_FLAGS_ALL, 2, 2, 2);
    if (systemServiceResult != ESP_OK) {
        Serial.printf("RainMaker system service failed: %s\n", esp_err_to_name(systemServiceResult));
    }

    // Check for saved Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(); 
    Serial.println("Checking for saved Wi-Fi credentials...");
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nAlready provisioned! Connected to Wi-Fi.");
        networkReady = true; 
    } else {
        Serial.println("\nNo Wi-Fi credentials found. Starting BLE Provisioning...");
        WiFi.onEvent(provisioningEvent);
        WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_NONE,
                                WIFI_PROV_SECURITY_1, "12345678", "PROV_SENSOR",
                                nullptr, nullptr, true);
    }
}

void loop() {
    if (!networkReady && WiFi.status() == WL_CONNECTED) {
        provisionedIp = WiFi.localIP();
        networkReady = true;
    }

    if (networkReady && !timeSyncStarted) {
        Serial.println("Starting NTP time synchronization...");
        configTime(0, 0, "216.239.35.0", "162.159.200.1", "129.6.15.28");
        timeSyncStarted = true;
        timeSyncDeadline = millis() + 30000;
    }

    startRainMakerAfterTimeSync();

    // --- Read Sensors and Update Display every 5 seconds ---
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 5000) {
        lastUpdate = millis();
        
        float t = dht.readTemperature();
        if (isnan(t)) t = 0.0f;
        
        // The module's digital output is active-low: LOW means light detected.
        int lightState = digitalRead(LDR_PIN);
        int lightPercent = (lightState == LOW) ? 100 : 0;

        // FIXED: Wrapped the values in value() so the library accepts them
        tempParam.updateAndReport(value((float)t));
        lightParam.updateAndReport(value((int)lightPercent));
        
        // Update OLED
        display.clearDisplay();
        display.setCursor(0,0);
        display.print("Temp: "); display.print(t); display.println(" C");
        display.print("Light: "); display.print(lightPercent); display.println(" %");
        display.print("Wi-Fi: "); display.println(WiFi.status() == WL_CONNECTED ? "OK" : "LOST");
        display.display();
        
        Serial.printf("Sent - Temp: %.2f, Light: %d%%\n", t, lightPercent);
    }
    
    delay(10);
}