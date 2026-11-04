#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

// ==== Cấu hình WiFi (Thay đổi theo mạng của bạn) ====
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ==== Webhook Discord (Thay bằng Webhook URL của kênh Discord bạn tạo) ====
const char* discordWebhookURL = "YOUR_DISCORD_WEBHOOK_URL";

// ==== Chân điều khiển IC dồn kênh CD4051 ====
#define S0 D5
#define S1 D6
#define S2 D7

// ==== Chân điều khiển Relay (Kích hoạt máy bơm / van châm dung dịch) ====
#define RELAY1_PIN D0  // Bật khi pH < 5.5 hoặc EC < 0.8 (dung dịch thiếu dưỡng chất hoặc quá axit)
#define RELAY2_PIN D2  // Bật khi pH > 7.0 (dung dịch quá kiềm)

// ==== Đầu vào tín hiệu Analog từ chân COM của CD4051 ====
#define ANALOG_IN A0

// ==== Thuật toán lọc Kalman 1 chiều đơn giản để làm mượt tín hiệu cảm biến ====
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

KalmanFilter kfPH, kfEC;

// ==== Biến lưu trữ giá trị đo ====
float filteredPH = 7.0;
float filteredEC = 1.0;
bool ecHighAlertSent = false;

unsigned long lastCheckTime = 0;
const unsigned long interval = 10000; // Chu kỳ đo: 10 giây / lần

// ==== Hàm chọn kênh đọc của CD4051 ====
void selectChannel(int channel) {
  digitalWrite(S0, channel & 0x01);
  digitalWrite(S1, (channel >> 1) & 0x01);
  digitalWrite(S2, (channel >> 2) & 0x01);
}

// ==== Hàm gửi tin nhắn cảnh báo tới Discord qua Webhook ====
void sendDiscordMessage(String content) {
  WiFiClientSecure client;
  client.setInsecure();  // Bỏ qua kiểm tra chứng chỉ SSL để ESP8266 gửi nhanh

  HTTPClient https;
  if (https.begin(client, discordWebhookURL)) {
    https.addHeader("Content-Type", "application/json");
    String payload = "{\"content\":\"" + content + "\"}";
    int httpCode = https.POST(payload);

    Serial.print("Discord POST Response Code: ");
    Serial.println(httpCode);
    https.end();
  } else {
    Serial.println("Không thể kết nối tới Discord Webhook.");
  }
}

// ==== Hàm đọc cảm biến, xử lý lọc và điều khiển Relay ====
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
  float ecValue = (voltageEC / 1000.0) / 2.0;  // mS/cm với K = 1.0
  ecValue *= 9.985;  // Hệ số K ước lượng theo phần cứng thực tế
  filteredEC = kfEC.updateEstimate(ecValue);

  // In thông tin gỡ lỗi ra Serial Monitor
  Serial.println("====== DỮ LIỆU CẢM BIẾN ======");
  Serial.print("pH (đã lọc): "); Serial.println(filteredPH, 2);
  Serial.print("EC (đã lọc): "); Serial.print(filteredEC, 2); Serial.println(" mS/cm");

  // ==== Logic điều khiển tự động Relay ====
  // Bật Relay1 nếu pH < 5.5 hoặc EC < 0.8
  if (filteredPH < 5.5 || filteredEC < 0.8) {
    digitalWrite(RELAY1_PIN, HIGH);  // Kích hoạt bơm dung dịch dinh dưỡng / tăng pH
    Serial.println("→ Bật Relay 1 (Cảnh báo: pH thấp hoặc EC thấp)");
  } else {
    digitalWrite(RELAY1_PIN, LOW);   // Ngắt bơm
  }

  // Bật Relay2 nếu pH > 7.0
  if (filteredPH > 7.0) {
    digitalWrite(RELAY2_PIN, HIGH);  // Kích hoạt bơm giảm pH
    Serial.println("→ Bật Relay 2 (Cảnh báo: pH cao)");
  } else {
    digitalWrite(RELAY2_PIN, LOW);   // Ngắt bơm
  }

  // Gửi cảnh báo Discord nếu độ dẫn điện EC vượt ngưỡng an toàn (> 1.5 mS/cm)
  if (filteredEC > 1.5 && !ecHighAlertSent) {
    sendDiscordMessage("🚨 CẢNH BÁO NÔNG NGHIỆP: Nồng độ dinh dưỡng EC quá cao: " + String(filteredEC, 2) + " mS/cm!");
    ecHighAlertSent = true;
  }

  // Reset cờ cảnh báo khi EC hạ về mức bình thường
  if (filteredEC <= 1.5 && ecHighAlertSent) {
    ecHighAlertSent = false;
    Serial.println("→ Chỉ số EC đã trở lại ngưỡng bình thường.");
  }
}

void setup() {
  Serial.begin(115200);

  // Cấu hình các chân điều khiển CD4051
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);

  // Cấu hình các chân Relay
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi đã kết nối thành công!");
}

void loop() {
  if (millis() - lastCheckTime >= interval) {
    sendSensorData();
    lastCheckTime = millis();
  }
}
