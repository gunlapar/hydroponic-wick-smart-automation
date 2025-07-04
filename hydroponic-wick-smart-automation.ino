#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <WiFiManager.h>          // WiFi Manager library
#include <ArduinoJson.h>          // Untuk parsing JSON di WiFi Manager

// ====== Konfigurasi WiFi (Fallback) ======
const char* fallback_ssid     = "your wifi";
const char* fallback_wifiPass = "wifi password";

// ====== Konfigurasi MQTT (Default) ======
char mqtt_server[40] = "your mqtt server";
char mqtt_port[6]    = "1883";
char mqtt_user[50]   = "your mqttuser";  
char mqtt_pass[50]   = "your mqtt pass";   

// ====== Topik MQTT ======
const char* topic_water_level    = "kel4/water_level";
const char* topic_temperature    = "kel4/suhu";
const char* topic_humidity       = "kel4/kelembapan";
const char* topic_pump_control   = "kel4/pompa/control";      // ON/OFF manual
const char* topic_pump_status    = "kel4/pompa/status";
const char* topic_mode_control   = "kel4/mode/control";       // AUTO/MANUAL
const char* topic_mode_status    = "kel4/mode/status";
const char* topic_threshold_set  = "kel4/threshold/set";      // format: "25,75"
const char* topic_threshold_get  = "kel4/threshold/status";
const char* topic_system_status  = "kel4/system/status";      // JSON status lengkap

// ====== Pin Konfigurasi ======
#define WATER_LEVEL_PIN A0   // Sensor level air ke A0
#define DHT_PIN         D4   // DHT11 data ke D4
#define RELAY_PIN       D2   // Relay pompa ke D2
#define CONFIG_BUTTON   D1   // Tombol untuk reset WiFi config (optional)

// ====== Inisialisasi DHT ======
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ====== Global vars ======
WiFiClient    espClient;
PubSubClient  client(espClient);
WiFiManager   wifiManager;

// ====== Timing ======
unsigned long lastMsg         = 0;
unsigned long pumpStartTime   = 0;
unsigned long pumpStopTime    = 0;
unsigned long buttonPressTime = 0;
const long    interval        = 5000;     // Interval publish sensor (5 detik)
const long    maxPumpRunTime  = 480000;   // 8 menit (480 detik) 480000
const long    minPumpRestTime = 10;       // 5 menit (300 detik) 300000
const long    buttonHoldTime  = 3000;     // 3 detik untuk reset WiFi

// ====== System State ======
enum PumpMode { AUTO, MANUAL };
PumpMode currentMode = AUTO;
bool pumpState = false;
bool manualPumpCommand = false;

// ====== Threshold Settings ======
int thresholdLow  = 25;  // Pompa ON jika water level < 25%
int thresholdHigh = 75;  // Pompa OFF jika water level > 75%

// ====== Safety Flags ======
bool sensorError = false;
bool pumpOverTime = false;
bool pumpInRestPeriod = false;

// ====== WiFi Manager Flags ======
bool shouldSaveConfig = false;

// Callback untuk menyimpan config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup_wifi_manager() {
  // Set callback untuk save config
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  // Custom parameters untuk MQTT
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 50);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqtt_pass, 50);
  
  // Tambahkan parameter ke WiFi Manager
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  
  // Set timeout untuk portal config (3 menit)
  wifiManager.setConfigPortalTimeout(180);
  
  // Set minimum kualitas sinyal
  wifiManager.setMinimumSignalQuality(20);
  
  // Auto connect dengan last saved credentials
  if (!wifiManager.autoConnect("SmartPump_Config", "12345678")) {
    Serial.println("Failed to connect to WiFi and hit timeout");
    // Fallback ke WiFi manual jika WiFi Manager gagal
    setup_wifi_fallback();
  }
  
  Serial.println("WiFi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Simpan parameter MQTT yang baru
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  
  if (shouldSaveConfig) {
    Serial.println("Saving MQTT config...");
    // Di sini Anda bisa menambahkan kode untuk menyimpan ke EEPROM/SPIFFS
    shouldSaveConfig = false;
  }
}

void setup_wifi_fallback() {
  Serial.println("Using fallback WiFi credentials...");
  WiFi.begin(fallback_ssid, fallback_wifiPass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Fallback WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Fallback WiFi connection failed!");
  }
}

void checkConfigButton() {
  // Cek apakah tombol config ditekan
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    if (buttonPressTime == 0) {
      buttonPressTime = millis();
    } else if (millis() - buttonPressTime > buttonHoldTime) {
      Serial.println("Config button held - Resetting WiFi settings...");
      wifiManager.resetSettings();
      ESP.restart();
    }
  } else {
    buttonPressTime = 0;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += char(payload[i]);
  }
  msg.toLowerCase();
  msg.trim();

  Serial.println("MQTT Received - Topic: " + String(topic) + " | Message: " + msg);

  // ====== Kontrol Mode ======
  if (String(topic) == topic_mode_control) {
    if (msg == "auto") {
      currentMode = AUTO;
      Serial.println("Mode changed to: AUTO");
    } else if (msg == "manual") {
      currentMode = MANUAL;
      Serial.println("Mode changed to: MANUAL");
    }
    publishModeStatus();
  }
  
  // ====== Kontrol Pompa Manual ======
  else if (String(topic) == topic_pump_control) {
    if (currentMode == MANUAL) {
      if (msg == "on" || msg == "1" || msg == "true" || msg == "nyala") {
        manualPumpCommand = true;
        Serial.println("Manual Pump Command: ON");
      } else if (msg == "off" || msg == "0" || msg == "false" || msg == "mati") {
        manualPumpCommand = false;
        Serial.println("Manual Pump Command: OFF");
      }
    } else {
      Serial.println("Pump control ignored - System in AUTO mode");
    }
  }
  
  // ====== Set Threshold ======
  else if (String(topic) == topic_threshold_set) {
    int commaIndex = msg.indexOf(',');
    if (commaIndex > 0) {
      int newLow = msg.substring(0, commaIndex).toInt();
      int newHigh = msg.substring(commaIndex + 1).toInt();
      
      if (newLow > 0 && newHigh > newLow && newHigh <= 100) {
        thresholdLow = newLow;
        thresholdHigh = newHigh;
        Serial.println("Threshold updated - Low: " + String(thresholdLow) + "% | High: " + String(thresholdHigh) + "%");
        publishThresholdStatus();
      } else {
        Serial.println("Invalid threshold format. Use: low,high (e.g., 25,75)");
      }
    }
  }
  
  // ====== WiFi Reset Command ======
  else if (String(topic) == "kel4/wifi/reset") {
    if (msg == "reset" || msg == "true" || msg == "1") {
      Serial.println("WiFi reset command received via MQTT");
      wifiManager.resetSettings();
      delay(1000);
      ESP.restart();
    }
  }
}

void controlPump(bool state) {
  if (state && !pumpState) {
    // Cek apakah pompa masih dalam periode istirahat
    if (pumpInRestPeriod && (millis() - pumpStopTime < minPumpRestTime)) {
      Serial.println("Pump in rest period. Cannot start yet.");
      return;
    }
    
    digitalWrite(RELAY_PIN, LOW);  // LOW = ON
    pumpState = true;
    pumpStartTime = millis();
    pumpInRestPeriod = false;
    pumpOverTime = false;
    Serial.println("Pompa: ON");
    
  } else if (!state && pumpState) {
    digitalWrite(RELAY_PIN, HIGH); // HIGH = OFF
    pumpState = false;
    pumpStopTime = millis();
    pumpInRestPeriod = true;
    Serial.println("Pompa: OFF");
  }
}

void checkPumpSafety() {
  // Cek apakah pompa sudah menyala terlalu lama
  if (pumpState && (millis() - pumpStartTime > maxPumpRunTime)) {
    controlPump(false);
    pumpOverTime = true;
    Serial.println("SAFETY: Pump stopped - Maximum runtime exceeded!");
  }
}

void autoModeLogic(int waterLevel) {
  if (sensorError) {
    Serial.println("AUTO Mode disabled - Sensor error");
    return;
  }
  
  if (pumpOverTime && waterLevel < thresholdHigh) {
    Serial.println("AUTO Mode suspended - Pump overtime, waiting for rest period");
    return;
  }
  
  // Hysteresis control untuk mencegah rapid cycling
  if (!pumpState && waterLevel < thresholdLow) {
    controlPump(true);
    Serial.println("AUTO: Water level low (" + String(waterLevel) + "%) - Starting pump");
  } 
  else if (pumpState && waterLevel > thresholdHigh) {
    controlPump(false);
    Serial.println("AUTO: Water level sufficient (" + String(waterLevel) + "%) - Stopping pump");
  }
}

void publishModeStatus() {
  client.publish(topic_mode_status, currentMode == AUTO ? "auto" : "manual");
}

void publishThresholdStatus() {
  String thresholdMsg = String(thresholdLow) + "," + String(thresholdHigh);
  client.publish(topic_threshold_get, thresholdMsg.c_str());
}

void publishSystemStatus(float temp, float humidity, int waterLevel) {
  // Publish JSON status lengkap
  String systemStatus = "{";
  systemStatus += "\"mode\":\"" + String(currentMode == AUTO ? "auto" : "manual") + "\",";
  systemStatus += "\"pump\":\"" + String(pumpState ? "on" : "off") + "\",";
  systemStatus += "\"water_level\":" + String(waterLevel) + ",";
  systemStatus += "\"temperature\":" + String(temp, 1) + ",";
  systemStatus += "\"humidity\":" + String(humidity, 1) + ",";
  systemStatus += "\"threshold_low\":" + String(thresholdLow) + ",";
  systemStatus += "\"threshold_high\":" + String(thresholdHigh) + ",";
  systemStatus += "\"sensor_error\":" + String(sensorError ? "true" : "false") + ",";
  systemStatus += "\"pump_overtime\":" + String(pumpOverTime ? "true" : "false") + ",";
  systemStatus += "\"rest_period\":" + String(pumpInRestPeriod ? "true" : "false") + ",";
  systemStatus += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  systemStatus += "\"ip_address\":\"" + WiFi.localIP().toString() + "\"";
  systemStatus += "}";
  
  client.publish(topic_system_status, systemStatus.c_str());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    
    // Convert port string to int
    int port = String(mqtt_port).toInt();
    if (port == 0) port = 1883; // default port
    
    if (String(mqtt_user).length() == 0) {
      if (client.connect("ESP8266Client")) {
        Serial.println("connected");
      }
    } else {
      if (client.connect("ESP8266Client", mqtt_user, mqtt_pass)) {
        Serial.println("connected with auth");
      }
    }
    
    if (client.connected()) {
      // Subscribe ke semua topic control
      client.subscribe(topic_pump_control);
      client.subscribe(topic_mode_control);
      client.subscribe(topic_threshold_set);
      client.subscribe("kel4/wifi/reset");  // Topic untuk reset WiFi
      
      // Publish status awal
      publishModeStatus();
      publishThresholdStatus();
      
      // Publish info koneksi
      String connectMsg = "SmartPump connected - IP: " + WiFi.localIP().toString();
      client.publish("kel4/system/connect", connectMsg.c_str());
      
    } else {
      Serial.print("failed rc=");
      Serial.print(client.state());
      Serial.println(" retry in 5s");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Smart Water Pump Controller with WiFi Manager ===");
  
  // Setup pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(CONFIG_BUTTON, INPUT_PULLUP);  // Tombol config dengan pull-up
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF saat start
  
  dht.begin();
  
  // Setup WiFi dengan WiFi Manager
  setup_wifi_manager();
  
  // Setup MQTT dengan parameter dari WiFi Manager
  int port = String(mqtt_port).toInt();
  if (port == 0) port = 1883;
  
  client.setServer(mqtt_server, port);
  client.setCallback(callback);
  
  Serial.println("=== Configuration ===");
  Serial.println("MQTT Server: " + String(mqtt_server));
  Serial.println("MQTT Port: " + String(mqtt_port));
  Serial.println("MQTT User: " + String(mqtt_user));
  Serial.println("Mode: AUTO");
  Serial.println("Threshold Low: " + String(thresholdLow) + "%");
  Serial.println("Threshold High: " + String(thresholdHigh) + "%");
  Serial.println("Setup complete");
  Serial.println("===================");
}

void loop() {
  // Cek koneksi WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Attempting reconnection...");
    setup_wifi_manager();
  }
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // Cek tombol config
  checkConfigButton();
  
  // Cek safety pompa
  checkPumpSafety();

  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    // ====== Baca Sensor Water Level ======
    int rawLevel = analogRead(WATER_LEVEL_PIN);
    int levelPct = map(constrain(rawLevel, 0, 800), 0, 800, 0, 100);
    
    // Deteksi sensor error (nilai tidak masuk akal)
    sensorError = (rawLevel < 5 || rawLevel > 1020);

    // ====== Baca DHT11 ======
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed reading DHT!");
      h = 0; t = 0;
    }

    // ====== Logic Control ======
    if (currentMode == AUTO && !sensorError) {
      autoModeLogic(levelPct);
    } else if (currentMode == MANUAL) {
      controlPump(manualPumpCommand);
    }

    // ====== Publish Data ======
    if (!sensorError) {
      Serial.printf("Mode: %s | Humidity: %.1f%% | Temp: %.1f°C | Water: %d%% | Pump: %s | WiFi: %ddBm\n", 
                    currentMode == AUTO ? "AUTO" : "MANUAL", h, t, levelPct, pumpState ? "ON" : "OFF", WiFi.RSSI());

      // Konversi ke string
      char bufT[8], bufH[8], bufL[8];
      dtostrf(t, 1, 2, bufT);
      dtostrf(h, 1, 2, bufH);
      dtostrf(levelPct, 1, 2, bufL);

      // Publish sensor data
      client.publish(topic_temperature, bufT);
      client.publish(topic_humidity, bufH);
      client.publish(topic_water_level, bufL);
    } else {
      Serial.println("SENSOR ERROR - Water level sensor malfunction!");
    }
    
    // Publish status
    client.publish(topic_pump_status, pumpState ? "on" : "off");
    publishSystemStatus(t, h, sensorError ? -1 : levelPct);
  }
}