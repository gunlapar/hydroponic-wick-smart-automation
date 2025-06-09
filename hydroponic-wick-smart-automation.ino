#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ====== Konfigurasi WiFi ======
const char* ssid       = "SIJA-PNP";
const char* wifiPass   = "12345678";

// ====== Konfigurasi MQTT ======
const char* mqtt_server   = "10.20.11.11";
const int   mqtt_port     = 1883;
const char* mqtt_user     = "onlykelompok4canusethisuser";  
const char* mqtt_pass     = "#123@#4";   

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

// ====== Inisialisasi DHT ======
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ====== Global vars ======
WiFiClient    espClient;
PubSubClient  client(espClient);

// ====== Timing ======
unsigned long lastMsg         = 0;
unsigned long pumpStartTime   = 0;
unsigned long pumpStopTime    = 0;
const long    interval        = 5000;     // Interval publish sensor (5 detik)
const long    maxPumpRunTime  = 480000;   // 8 menit (480 detik) 480000
const long    minPumpRestTime = 10;   // 5 menit (300 detik) 300000

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

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, wifiPass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
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
  systemStatus += "\"rest_period\":" + String(pumpInRestPeriod ? "true" : "false");
  systemStatus += "}";
  
  client.publish(topic_system_status, systemStatus.c_str());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
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
      
      // Publish status awal
      publishModeStatus();
      publishThresholdStatus();
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
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF saat start
  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  Serial.println("=== Smart Water Pump Controller ===");
  Serial.println("Mode: AUTO");
  Serial.println("Threshold Low: " + String(thresholdLow) + "%");
  Serial.println("Threshold High: " + String(thresholdHigh) + "%");
  Serial.println("Setup complete");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
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
      Serial.printf("Mode: %s | Humidity: %.1f%% | Temp: %.1f°C | Water: %d%% | Pump: %s\n", 
                    currentMode == AUTO ? "AUTO" : "MANUAL", h, t, levelPct, pumpState ? "ON" : "OFF");

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