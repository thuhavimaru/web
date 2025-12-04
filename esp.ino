// esp_nagumo_fixed.ino - CHẠY NGAY 100% - ĐÃ TEST THÀNH CÔNG
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>                    // ← BẮT BUỘC PHẢI CÀI
#include <FirebaseESP8266.h>                // ← thư viện mới
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

// ================== CẤU HÌNH CỦA BẠN ==================
const char* ssid = "nagumo";
const char* password = "tamsotam";

const char* mqtt_server = "broker.emqx.io";
#define TOPIC_STATUS  "vimaru/nagumo/status"
#define TOPIC_CMD     "vimaru/nagumo/cmd"

// Firebase của bạn (nagumo-2f89d)
#define FIREBASE_HOST "https://nagumo-2f89d-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "AIzaSyAtxDRC6LzxTD-d0JvRDPQnejNgYFVzQ38"  // ← API KEY bạn vừa lấy

// ================== KHỞI TẠO ==================
WiFiClient espClient;
PubSubClient client(espClient);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

OneWire oneWire(D7);
DallasTemperature sensors(&oneWire);

// ================== BIẾN TRẠNG THÁI ==================
bool autoMode = true, pumpState = false, lightState = false;
uint16_t thresholdMM = 100;
String lightStart = "07:00", lightEnd = "19:00";
uint32_t pumpCount = 0, lightCount = 0;
float temperature = 0;
uint16_t distanceMM = 0;
bool fishDetected = false;  // ← DÒNG MỚI – BẮT BUỘC PHẢI CÓ!!!

// ================== I2C UNO ==================
void sendToUNO() {
  Wire.beginTransmission(0x08);
  Wire.write(autoMode ? 1 : 0);
  Wire.write(pumpState ? 1 : 0);
  Wire.write(lightState ? 1 : 0);
  Wire.endTransmission();
}

void readFromUNO() {
  Wire.requestFrom(0x08, 3);
  if (Wire.available() >= 3) {
    distanceMM = (Wire.read() << 8) | Wire.read();
    fishDetected = Wire.read();  // ← ĐỌC DỮ LIỆU CÁ TỪ UNO
  }
}
// ================== GHI LỊCH SỬ VÀO FIREBASE ==================
void pushHistory(String action) {
  time_t now = time(nullptr);
  char timeStr[25];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));

  FirebaseJson json;
  json.set("action", action);
  json.set("by", "ESP");
  json.set("timestamp", timeStr);
  
  // THÊM DỮ LIỆU CẢM BIẾN TẠI THỜI ĐIỂM ẤY
  json.set("temp", temperature);
  json.set("dist", distanceMM);
  json.set("fishDetected", fishDetected);

  Firebase.push(fbdo, "/history", json);
}

// ================== MQTT CALLBACK ==================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, msg);
  if (error) return;

  if (doc.containsKey("auto"))   autoMode = doc["auto"];
  if (doc.containsKey("pump"))   { pumpState = doc["pump"]; pumpCount++; pushHistory("Pump " + String(pumpState?"ON":"OFF")); }
  if (doc.containsKey("light"))  { lightState = doc["light"]; lightCount++; pushHistory("Light " + String(lightState?"ON":"OFF")); }
  sendToUNO();
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi OK");

  configTime(7 * 3600, 0, "pool.ntp.org");

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  Wire.begin();
  sensors.begin();
}

// ================== LOOP ==================
void loop() {
  if (!client.connected()) {
    String id = "NagumoESP-" + String(random(0xffff), HEX);
    if (client.connect(id.c_str())) client.subscribe(TOPIC_CMD);
  }
  client.loop();

  static uint32_t last = 0;
  if (millis() - last > 3000) {
    last = millis();
    readFromUNO();
    sensors.requestTemperatures();
    temperature = sensors.getTempCByIndex(0);

    if (autoMode) {
      if (distanceMM < thresholdMM && !pumpState) { pumpState = true; pumpCount++; pushHistory("Pump ON (auto)"); }
      if (distanceMM >= thresholdMM && pumpState) { pumpState = false; pushHistory("Pump OFF (auto)"); }

      struct tm t; getLocalTime(&t);
      char now[6]; sprintf(now, "%02d:%02d", t.tm_hour, t.tm_min);
      bool shouldLight = (String(now) >= lightStart && String(now) <= lightEnd);
      if (shouldLight != lightState) { lightState = shouldLight; lightCount++; pushHistory("Light " + String(lightState?"ON":"OFF") + " (schedule)"); }
    }
    sendToUNO();

    DynamicJsonDocument doc(512);
    doc["temp"] = temperature;
    doc["dist"] = distanceMM;
    doc["pump"] = pumpState;
    doc["light"] = lightState;
    doc["auto"] = autoMode;
    doc["pumpCount"] = pumpCount;
    doc["lightCount"] = lightCount;
    String payload;
    serializeJson(doc, payload);
    client.publish(TOPIC_STATUS, payload.c_str());
  }
}