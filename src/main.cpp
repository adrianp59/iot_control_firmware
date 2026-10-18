#include <Arduino.h>
#include <WiFiS3.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <LiquidCrystal.h>
#include <WDT.h>
#include "secrets.h"
#include "ac_commands.h"

#define IR_PIN 9

const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const char* ssid = WIFI_SSID;
const char* pass = WIFI_PASS;
const char* mqtt_server = MQTT_SERVER;

const char* mqtt_user = MQTT_USER;
const char* mqtt_pass = MQTT_PASS;

const char* topic_health_rssi = "home/sensors/home-living/health/wifi_rssi";
const char* topic_health_uptime = "home/sensors/home-living/health/uptime_s";
const unsigned long health_publish_interval = 30000;
const unsigned long bme_publish_interval = 10000;
const unsigned long mqtt_reconnect_interval = 5000;

unsigned long lastHealthMsg = 0;
unsigned long lastBmeMsg = 0;
unsigned long lastMqttReconnectAttempt = 0;

int status = WL_IDLE_STATUS;

float currentTemp = 0.0;
float currentHum = 0.0;
String lastExecutedCommand = "None";
String systemStatus = "Booting...";

// Initialize WiFi and MQTT clients with TLS support
WiFiSSLClient wifiClient; 
PubSubClient client(wifiClient);
Adafruit_BME280 bme;

bool shouldSendIR = false;
String pendingCommand = "";

void sendMitsubishiAC(uint32_t* targetArray);
void updateDisplay();

struct CommandMapping {
    const char* name;
    uint32_t* payload;
};

#define MAP_CMD(cmd_name) {#cmd_name, cmd_name}

static CommandMapping kCommandMappings[] = {
    MAP_CMD(ac_off),
    MAP_CMD(ac_cool_25), MAP_CMD(ac_cool_25_high), MAP_CMD(ac_cool_25_eco),
    MAP_CMD(ac_cool_24), MAP_CMD(ac_cool_24_high), MAP_CMD(ac_cool_24_eco),
    MAP_CMD(ac_cool_23), MAP_CMD(ac_cool_23_high), MAP_CMD(ac_cool_23_eco),
    MAP_CMD(ac_cool_22), MAP_CMD(ac_cool_22_high), MAP_CMD(ac_cool_22_eco),
    MAP_CMD(ac_cool_21), MAP_CMD(ac_cool_21_high), MAP_CMD(ac_cool_21_eco),
    MAP_CMD(ac_cool_20), MAP_CMD(ac_cool_20_high), MAP_CMD(ac_cool_20_eco),
    MAP_CMD(ac_cool_19), MAP_CMD(ac_cool_19_high), MAP_CMD(ac_cool_19_eco),
    MAP_CMD(ac_cool_18), MAP_CMD(ac_cool_18_high), MAP_CMD(ac_cool_18_eco),
    MAP_CMD(ac_heat_30), MAP_CMD(ac_heat_30_high), MAP_CMD(ac_heat_30_eco),
    MAP_CMD(ac_heat_29), MAP_CMD(ac_heat_29_high), MAP_CMD(ac_heat_29_eco),
    MAP_CMD(ac_heat_28), MAP_CMD(ac_heat_28_high), MAP_CMD(ac_heat_28_eco),
    MAP_CMD(ac_heat_27), MAP_CMD(ac_heat_27_high), MAP_CMD(ac_heat_27_eco),
    MAP_CMD(ac_heat_26), MAP_CMD(ac_heat_26_high), MAP_CMD(ac_heat_26_eco),
    MAP_CMD(ac_heat_25), MAP_CMD(ac_heat_25_high), MAP_CMD(ac_heat_25_eco),
    MAP_CMD(ac_heat_24), MAP_CMD(ac_heat_24_high), MAP_CMD(ac_heat_24_eco),
    MAP_CMD(ac_heat_23), MAP_CMD(ac_heat_23_high), MAP_CMD(ac_heat_23_eco),
    MAP_CMD(ac_heat_22), MAP_CMD(ac_heat_22_high), MAP_CMD(ac_heat_22_eco),
    MAP_CMD(ac_heat_21), MAP_CMD(ac_heat_21_high), MAP_CMD(ac_heat_21_eco),
    MAP_CMD(ac_heat_20), MAP_CMD(ac_heat_20_high), MAP_CMD(ac_heat_20_eco),
    MAP_CMD(ac_dry_26),  MAP_CMD(ac_dry_26_eco),
    MAP_CMD(ac_dry_25),  MAP_CMD(ac_dry_25_eco),
    MAP_CMD(ac_dry_24),  MAP_CMD(ac_dry_24_eco),
    MAP_CMD(ac_dry_23),  MAP_CMD(ac_dry_23_eco),
    MAP_CMD(ac_dry_22),  MAP_CMD(ac_dry_22_eco),
    MAP_CMD(ac_auto_p_6), MAP_CMD(ac_auto_p_5), MAP_CMD(ac_auto_p_4),
    MAP_CMD(ac_auto_p_3), MAP_CMD(ac_auto_p_2), MAP_CMD(ac_auto_p_1),
    MAP_CMD(ac_auto_p_0), MAP_CMD(ac_auto_m_1), MAP_CMD(ac_auto_m_2),
    MAP_CMD(ac_auto_m_3), MAP_CMD(ac_auto_m_4), MAP_CMD(ac_auto_m_5),
    MAP_CMD(ac_auto_m_6)
};

#undef MAP_CMD
const size_t kCommandMappingsCount = sizeof(kCommandMappings) / sizeof(kCommandMappings[0]);

void determineCommandFromMQTT(const char* command) {
    String normalizedCommand = command == nullptr ? "" : String(command);
    normalizedCommand.trim();

    if (normalizedCommand.length() == 0) {
        Serial.println("Received empty command.");
        client.publish("home/sensors/home-living/status", "EMPTY COMMAND");
        return;
    }

    for (size_t i = 0; i < kCommandMappingsCount; i++) {
        if (normalizedCommand.equals(kCommandMappings[i].name)) {
            sendMitsubishiAC(kCommandMappings[i].payload);
            lastExecutedCommand = normalizedCommand;
            if (lastExecutedCommand.startsWith("ac_")) {
                lastExecutedCommand = lastExecutedCommand.substring(3); 
            }
            Serial.println("Signal sent for: " + normalizedCommand);
            return;
        }
    }

    Serial.println("Unknown command received: " + normalizedCommand);
    client.publish("home/sensors/home-living/status", "UNKNOWN COMMAND");
}

void pulse38kHz(uint32_t microSeconds) {
    uint32_t endTime = micros() + microSeconds;
    while (micros() < endTime) {
        digitalWrite(IR_PIN, HIGH);
        delayMicroseconds(12);
        digitalWrite(IR_PIN, LOW);
        delayMicroseconds(12);
    }
}

void sendMitsubishiAC(uint32_t* targetArray) {
    pulse38kHz(3200);
    delayMicroseconds(1600);

    for (int i = 0; i < 88; i++) {
        int arrayIndex = i / 32;
        int bitIndex   = i % 32;
        bool bitValue  = (targetArray[arrayIndex] >> bitIndex) & 1;

        pulse38kHz(400);
        if (bitValue) {
            delayMicroseconds(1200);
        } else {
            delayMicroseconds(400);
        }
    }

    pulse38kHz(400);
    digitalWrite(IR_PIN, LOW);
}

void publishHealthMetrics() {
    if (!client.connected()) return;
    
    long rssi = WiFi.RSSI();
    unsigned long uptimeSeconds = millis() / 1000;

    char rssiStr[10];
    char uptimeStr[20];
    snprintf(rssiStr, sizeof(rssiStr), "%ld", rssi);
    snprintf(uptimeStr, sizeof(uptimeStr), "%lu", uptimeSeconds);

    client.publish(topic_health_rssi, rssiStr);
    client.publish(topic_health_uptime, uptimeStr);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    pendingCommand = "";
    for (unsigned int i = 0; i < length; i++) {
        pendingCommand += (char)payload[i];
    }
    pendingCommand.trim();
    shouldSendIR = true;
    Serial.println("Received command: " + pendingCommand);
}

bool tryMQTTConnect() {
    Serial.print("Attempting MQTT connection (TLS)...");
    systemStatus = "Conn MQTT...";
    updateDisplay();
    
    // Attempt to connect to the MQTT broker with TLS and authentication
    if (client.connect("ArduinoClient_LivingRoom", "add_your_username_here", "add_your_password_here")) {
        Serial.println("connected securely!");
        client.subscribe("home/sensors/home-living/ac_control");
        systemStatus = "Online";
        updateDisplay();
        return true;
    }
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" will retry");
    systemStatus = "MQTT Err";
    updateDisplay();
    return false;
}

void handleNetworkReconnections() {
    if (WiFi.status() != WL_CONNECTED) {
        systemStatus = "WiFi Lost";
        updateDisplay();
        WiFi.disconnect();
        WiFi.begin(ssid, pass);
        return; 
    }

    if (!client.connected()) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastMqttReconnectAttempt > mqtt_reconnect_interval) {
            lastMqttReconnectAttempt = currentMillis;
            if (tryMQTTConnect()) {
                lastMqttReconnectAttempt = 0;
            }
        }
    } else {
        client.loop();
    }
}

void updateDisplay() {
    lcd.clear();
    
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(currentTemp, 1);
    lcd.print("C  H:");
    lcd.print(currentHum, 1);
    lcd.print("%");
    
    lcd.setCursor(0, 1);
    lcd.print(systemStatus);
    
    if (lastExecutedCommand != "None") {
        lcd.setCursor(8, 1);
        lcd.print("IR:");
        lcd.print(lastExecutedCommand.substring(0, 5)); 
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(IR_PIN, OUTPUT);
    digitalWrite(IR_PIN, LOW);

    lcd.begin(16, 2);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Booting System...");
    
    systemStatus = "Conn WiFi...";
    updateDisplay();
    
    unsigned long bootWifiTimeout = millis();
    while (status != WL_CONNECTED) {
        status = WiFi.begin(ssid, pass);
        delay(1500);
        if (millis() - bootWifiTimeout > 15000) { 
            break;
        }
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected");
        systemStatus = "WiFi OK";
        updateDisplay();
    }

    if (!bme.begin(0x77)) {
        Serial.println("BME280 error! Checking alternative address...");
        if (!bme.begin(0x76)) {
            Serial.println("BME280 failure. Running without sensors.");
            systemStatus = "BME280 Err";
            updateDisplay();
        }
    }

    client.setServer(mqtt_server, 8883);
    client.setCallback(mqttCallback);

    WDT.begin(4000);
}

void loop() {
    WDT.refresh();
    handleNetworkReconnections();

    unsigned long currentMillis = millis();

    if (currentMillis - lastBmeMsg > bme_publish_interval) {
        lastBmeMsg = currentMillis;
        
        float t = bme.readTemperature();
        float h = bme.readHumidity();
        
        if (!isnan(t) && !isnan(h)) {
            currentTemp = t;
            currentHum = h;
            updateDisplay();

            if (client.connected()) {
                char msg[10];
                dtostrf(currentTemp, 1, 2, msg);
                client.publish("home/sensors/home-living/temp", msg);

                dtostrf(currentHum, 1, 2, msg);
                client.publish("home/sensors/home-living/humidity", msg);
            }
        }
    }

    if (currentMillis - lastHealthMsg > health_publish_interval) {
        lastHealthMsg = currentMillis;
        publishHealthMetrics();
        updateDisplay(); 
    }

    if (shouldSendIR) {
        shouldSendIR = false;
        
        String oldStatus = systemStatus;
        systemStatus = "TX IR...";
        updateDisplay();
        
        determineCommandFromMQTT(pendingCommand.c_str());
        
        if (client.connected()) {
            client.publish("home/sensors/home-living/status", "EXECUTED SUCCESSFULLY");
            systemStatus = "Online";
        } else {
            systemStatus = oldStatus;
        }
        updateDisplay();
    }
}