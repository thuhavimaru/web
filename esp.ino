// esp_nagumo_ULTIMATE.ino
// ĐÃ TÍCH HỢP: NÚT VẬT LÝ + AUTO + MQTT + FIREBASE + WEB CẬP NHẬT NGAY KHI BẤM NÚT!!!

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FirebaseESP8266.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

// ================== CẤU HÌNH ==================
const char* ssid = "nagumo";
const char* password = "tamsotam";

const char* mqtt_server = "broker.emqx.io";
#define TOPIC_STATUS  "vimaru/nagumo/status"
#define TOPIC_CMD     "vimaru/nagumo/cmd"

#define FIREBASE_HOST "https://nagumo-2f89d-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "AIzaSyAtxDRC6LzxTD-d0JvRDPQnejNgYFVzQ38"

// NÚT VẬT LÝ
#define BTN_AUTO  D4
#define BTN_PUMP  D5
#define BTN_LIGHT D6

// DS18B20
#define DS_PIN D7
OneWire oneWire(DS_PIN);
DallasTemperature sensors(&oneWire);

// I2C UNO
#define UNO_ADDR 0x08

// ================== KHỞI TẠO ==================
WiFiClient espClient;
PubSubClient client(espClient);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================== BIẾN TRẠNG THÁI ==================
bool autoMode = true, pumpState = false, lightState = false;
bool oldAuto = false, oldPump = false, oldLight = false;  // để phát hiện thay đổi
uint16_t thresholdMM = 100;
String lightStart = "07:00", lightEnd = "19:00";
uint32_t pumpCount = 0, lightCount = 0;
float temperature = 0;
uint16_t distanceMM = 0;
bool fishDetected = false;

// ================== I2C & CẢM BIẾN ==================
void sendToUNO() {
  Wire.beginTransmission(UNO_ADDR);
  Wire.write(autoMode ? 1 : 0);
  Wire.write(pumpState ? 1 : 0);
  Wire.write(lightState ? 1 : 0);
  Wire.endTransmission();
}

void readFromUNO() {
  Wire.requestFrom(UNO_ADDR, 3);
  if (Wire.available() >= 3) {
    distanceMM = (Wire.read() << 8) | Wire.read();
    fishDetected = Wire.read();
  }
}

// ================== GHI LỊCH SỬ + GỬI TRẠNG THÁI ==================
void pushHistory(String action) {
  time_t now = time(nullptr);
  char timeStr[25];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));

  FirebaseJson json;
  json.set("action", action);
  json.set("by", "ESP");
  json.set("timestamp", timeStr);
  json.set("temp", temperature);
  json.set("dist", distanceMM);
  json.set("fishDetected", fishDetected);
  Firebase.push(fbdo, "/history", json);
}

void publishStatus() {
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

// ================== NÚT VẬT LÝ ==================
void handleButtons() {
  bool a = digitalRead(BTN_AUTO);
  bool p = digitalRead(BTN_PUMP);
  bool l = digitalRead(BTN_LIGHT);

  if (a == LOW && oldAuto == HIGH) { delay(50); if (digitalRead(BTN_AUTO) == LOW) { autoMode = !autoMode; pushHistory("Auto mode " + String(autoMode?"ON":"OFF") + " (nút vật lý)"); publishStatus(); } }
  if (p == LOW && oldPump == HIGH && !autoMode) { delay(50); if (digitalRead(BTN_PUMP) == LOW) { pumpState = !pumpState; pumpCount++; pushHistory("Pump " + String(pumpState?"ON":"OFF") + " (nút vật lý)"); publishStatus(); } }
  if (l == LOW && oldLight == HIGH && !autoMode) { delay(50); if (digitalRead(BTN_LIGHT) == LOW) { lightState = !lightState; lightCount++; pushHistory("Light " + String(lightState?"ON":"OFF") + " (nút vật lý)"); publishStatus(); } }

  oldAuto = a; oldPump = p; oldLight = l;
}

// ================== MQTT CALLBACK ==================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  DynamicJsonDocument doc(512);
  deserializeJson(doc, msg);

  if (doc.containsKey("auto"))   { autoMode = doc["auto"]; pushHistory("Auto mode " + String(autoMode?"ON":"OFF") + " (web)"); }
  if (doc.containsKey("pump"))   { pumpState = doc["pump"]; pumpCount++; pushHistory("Pump " + String(pumpState?"ON":"OFF") + " (web)"); }
  if (doc.containsKey("light"))  { lightState = doc["light"]; lightCount++; pushHistory("Light " + String(lightState?"ON":"OFF") + " (web)"); }

  publishStatus();
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_AUTO, INPUT_PULLUP);
  pinMode(BTN_PUMP, INPUT_PULLUP);
  pinMode(BTN_LIGHT, INPUT_PULLUP);

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
    if (client.connect(id.c_str())) {
      client.subscribe(TOPIC_CMD);
      publishStatus();
    }
  }
  client.loop();

  handleButtons();  // ← ĐỌC NÚT VẬT LÝ

  static uint32_t last = 0;
  if (millis() - last > 3000) {
    last = millis();
    readFromUNO();
    sensors.requestTemperatures();
    temperature = sensors.getTempCByIndex(0);

    if (autoMode) {
      if (distanceMM < thresholdMM && !pumpState) { pumpState = true; pumpCount++; pushHistory("Pump ON (auto)"); publishStatus(); }
      if (distanceMM >= thresholdMM && pumpState) { pumpState = false; pushHistory("Pump OFF (auto)"); publishStatus(); }

      struct tm t; getLocalTime(&t);
      char now[6]; sprintf(now, "%02d:%02d", t.tm_hour, t.tm_min);
      bool shouldLight = (String(now) >= lightStart && String(now) <= lightEnd);
      if (shouldLight != lightState) { lightState = shouldLight; lightCount++; pushHistory("Light " + String(lightState?"ON":"OFF") + " (schedule)"); publishStatus(); }
    }

    sendToUNO();
    publishStatus(); // gửi realtime mỗi 3s
  }
}