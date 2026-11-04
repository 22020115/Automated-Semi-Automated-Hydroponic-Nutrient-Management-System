#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "AutoECpH"
#define BLYNK_AUTH_TOKEN "YOUR_AUTH_TOKEN"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

// ==== Chân điều khiển IC dồn kênh CD4051 ====
#define S0 D5
#define S1 D6
#define S2 D7

// ==== Chân điều khiển Relay ====
#define RELAY1_PIN D0
#define RELAY2_PIN D2

// ==== Đầu vào tín hiệu Analog từ chân COM của CD4051 ====
#define ANALOG_IN A0

// ==== Cấu hình WiFi ====
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

BlynkTimer timer;

// ==== Thuật toán lọc Kalman 1 chiều đơn giản ====
class KalmanFilter {
  float estimate;
  float errorEstimate;
  float q;  // Nhiễu quá trình (process noise)
  float r;  // Nhiễu đo lường (measurement noise)

public:
  KalmanFilter() {
    estimate = 0;
    errorEstimate = 1;
    q = 0.01;
    r = 0.5;
  }

  float updateEstimate(float measurement) {
    float kalmanGain = errorEstimate / (errorEstimate + r);
    estimate = estimate + kalmanGain * (measurement - estimate);
    errorEstimate = (1 - kalmanGain) * errorEstimate + fabs(estimate - measurement) * q;
    return estimate;
  }
};

// Biến lưu trữ giá trị đo
float filteredPH = 7.0;
float filteredEC = 1.0;

KalmanFilter kfPH, kfEC;

// Quản lý chu kỳ đo
unsigned long lastCheckTime = 0;
unsigned long interval = 30000; // Mặc định kiểm tra mỗi 30 giây
bool isPH_EC_OK = false;

// Hàm chọn kênh đọc của CD4051
void selectChannel(int channel) {
  digitalWrite(S0, channel & 0x01);
  digitalWrite(S1, (channel >> 1) & 0x01);
  digitalWrite(S2, (channel >> 2) & 0x01);
}

// Hàm gửi cảnh báo sự kiện lên ứng dụng Blynk
void sendWarning(const char* event, const char* message) {
  Blynk.logEvent(event, message);
}

// Hàm đọc cảm biến và gửi dữ liệu lên Blynk Dashboard
void sendSensorData() {
  const float VOLTAGE_DIVIDER_RATIO = 5.0;

  // 1. Đọc cảm biến pH (Kênh 0 của CD4051)
  selectChannel(0);
  delay(10);
  int rawPH = analogRead(ANALOG_IN);
  float voltagePH = rawPH * 1000.0 / 1024.0 * VOLTAGE_DIVIDER_RATIO;
  float delta_mV = 1500.0 - voltagePH;
  float pHValue = 7.0 + (delta_mV / 59.16);
  pHValue = constrain(pHValue, 0.0, 14.0);
  filteredPH = kfPH.updateEstimate(pHValue);

  // 2. Đọc cảm biến EC (Kênh 1 của CD4051)
  selectChannel(1);
  delay(10);
  int rawEC = analogRead(ANALOG_IN);
  float voltageEC = rawEC * 1000.0 / 1024.0;
  float ecValue = (voltageEC / 1000.0) / 2.0;
  ecValue *= 9.985;  // Hệ số K ước lượng theo phần cứng
  filteredEC = kfEC.updateEstimate(ecValue);

  // Đẩy dữ liệu lên các chân ảo (Virtual Pins) trên Blynk
  Blynk.virtualWrite(V3, filteredEC);
  Blynk.virtualWrite(V4, filteredPH);

  // Debug ra Serial Monitor
  Serial.println("------ DỮ LIỆU CẢM BIẾN (BLYNK) ------");
  Serial.print("pH: "); Serial.println(filteredPH, 2);
  Serial.print("EC: "); Serial.print(filteredEC, 2); Serial.println(" mS/cm");
  Serial.println("--------------------------------------\n");

  // Kiểm tra ngưỡng cảnh báo
  if (filteredPH < 5.5) {
    sendWarning("canh_bao_ph", "⚠️ pH quá thấp (< 5.5)!");
    isPH_EC_OK = false;
  } else if (filteredPH > 7.0) {
    sendWarning("canh_bao_ph", "⚠️ pH quá cao (> 7.0)!");
    isPH_EC_OK = false;
  }

  if (filteredEC < 0.8) {
    sendWarning("canh_bao_ec", "⚠️ EC quá thấp (< 0.8 mS/cm)!");
    isPH_EC_OK = false;
  } else if (filteredEC > 1.5) {
    sendWarning("canh_bao_ec", "⚠️ EC quá cao (> 1.5 mS/cm)!");
    isPH_EC_OK = false;
  }

  // Điều chỉnh tần suất đo: Nếu bình thường thì giãn chu kỳ đo, nếu bất thường thì đo thường xuyên
  if (filteredPH >= 5.5 && filteredPH <= 7.0 && filteredEC >= 0.8 && filteredEC <= 1.5) {
    if (!isPH_EC_OK) {
      Serial.println("✅ pH và EC đã ổn định trong ngưỡng an toàn.");
    }
    isPH_EC_OK = true;
    interval = 10800000; // 3 giờ đo 1 lần để tiết kiệm năng lượng và kéo dài tuổi thọ đầu dò
  } else {
    interval = 30000;    // 30 giây đo 1 lần để giám sát liên tục khi có bất thường
  }

  lastCheckTime = millis();
}

// ==== CHẾ ĐỘ BÁN TỰ ĐỘNG / ĐIỀU KHIỂN BẬT TAY TỪ GIAO DIỆN BLYNK ====
// Nút bấm ảo V1: Bật / Tắt thủ công Relay 1 (Bơm dinh dưỡng / tăng pH)
BLYNK_WRITE(V1) {
  int state = param.asInt();
  // Module relay kích mức LOW (Active LOW) hoặc HIGH tùy loại module
  digitalWrite(RELAY1_PIN, state ? LOW : HIGH);
  Serial.println(state ? "→ Điều khiển tay: Bật Relay 1" : "→ Điều khiển tay: Tắt Relay 1");
}

// Nút bấm ảo V2: Bật / Tắt thủ công Relay 2 (Bơm nước / giảm pH)
BLYNK_WRITE(V2) {
  int state = param.asInt();
  digitalWrite(RELAY2_PIN, state ? LOW : HIGH);
  Serial.println(state ? "→ Điều khiển tay: Bật Relay 2" : "→ Điều khiển tay: Tắt Relay 2");
}

void setup() {
  Serial.begin(115200);

  // Khởi tạo các chân điều khiển CD4051
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);

  // Khởi tạo các chân Relay
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // Khởi tạo kết nối Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  lastCheckTime = millis();
}

void loop() {
  Blynk.run();
  timer.run();

  if (millis() - lastCheckTime >= interval) {
    sendSensorData();
  }
}
