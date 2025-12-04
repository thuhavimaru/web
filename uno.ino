// uno_nagumo_final.ino
// DÀNH RIÊNG CHO DỰ ÁN NAGUMO - VIMARU 2025
// Chỉ nhận lệnh từ ESP8266 – KHÔNG tự điều khiển gì nữa → 0% lỗi nháy relay

#include <Wire.h>

#define RELAY_PUMP   2    // Relay bơm nước
#define RELAY_LIGHT  3    // Relay đèn
#define TRIG_PIN     10   // HC-SR04 Trig
#define ECHO_PIN     11   // HC-SR04 Echo

uint16_t distanceMM = 0;
bool fishDetected = false;

bool pumpState = false;   // Nhận từ ESP
bool lightState = false;  // Nhận từ ESP
bool autoMode = true;     // Chỉ để debug

void readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30ms
  if (duration == 0) {
    distanceMM = 9999; // lỗi cảm biến
  } else {
    distanceMM = duration * 0.343 / 2; // mm
  }
  fishDetected = (distanceMM < 150); // chỉ để gửi ESP biết
}

// ====== NHẬN LỆNH TỪ ESP8266 ======
void receiveData(int howMany) {
  if (howMany >= 3) {
    autoMode    = Wire.read();  // 1 = Auto, 0 = Manual
    pumpState   = Wire.read();  // 1 = ON, 0 = OFF
    lightState  = Wire.read();  // 1 = ON, 0 = OFF
  }
}

// ====== GỬI DỮ LIỆU SIÊU ÂM VỀ ESP8266 ======
void sendData() {
  Wire.write(distanceMM >> 8);    // byte cao
  Wire.write(distanceMM & 0xFF);  // byte thấp
  Wire.write(fishDetected ? 1 : 0);
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PUMP,  OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(TRIG_PIN,    OUTPUT);
  pinMode(ECHO_PIN,    INPUT);

  digitalWrite(RELAY_PUMP,  LOW);
  digitalWrite(RELAY_LIGHT, LOW);

  Wire.begin(0x08);                    // I2C address = 8
  Wire.onReceive(receiveData);         // khi ESP gửi lệnh
  Wire.onRequest(sendData);            // khi ESP hỏi dữ liệu

  Serial.println("UNO NAGUMO READY – CHỜ LỆNH TỪ ESP8266");
}

void loop() {
  readUltrasonic();

  // CHỈ LÀM THEO LỆNH TỪ ESP → KHÔNG TỰ QUYẾT ĐỊNH GÌ
  digitalWrite(RELAY_PUMP,  pumpState  ? HIGH : LOW);
  digitalWrite(RELAY_LIGHT, lightState ? HIGH : LOW);

  // Debug Serial (mở Serial Monitor 115200 để xem)
  Serial.print("Mực nước: ");
  Serial.print(distanceMM);
  Serial.print(" mm | Bơm: ");
  Serial.print(pumpState ? "CHẠY" : "DỪNG");
  Serial.print(" | Đèn: ");
  Serial.println(lightState ? "BẬT" : "TẮT");

  delay(300);
}