# Hệ Thống Giám Sát và Điều Chỉnh pH - EC Thủy Canh (Tự Động & Bán Tự Động)

> **Đồ án môn học:** Vi Điều Khiển / IoT  
> **Sinh viên thực hiện:** Nguyễn Sơn Tùng  
> **Nền tảng phần cứng:** ESP8266 (NodeMCU) | Cảm biến pH | Cảm biến EC | IC dồn kênh CD4051 | Module Relay  
> **Đánh giá:** Điểm A+ (9.0/10)

---

## 📌 1. Giới thiệu tổng quan

Trong canh tác nông nghiệp công nghệ cao và thủy canh, hai chỉ số quan trọng quyết định sự phát triển của cây trồng là:
* **Độ pH:** Ảnh hưởng trực tiếp đến khả năng hấp thụ dinh dưỡng của rễ cây (ngưỡng tối ưu thường từ `5.5 - 6.5`).
* **Độ dẫn điện EC (Electrical Conductivity):** Phản ánh tổng lượng ion khoáng dinh dưỡng hòa tan trong nước (ngưỡng an toàn thường từ `0.8 - 1.5 mS/cm`).

Dự án này xây dựng một hệ thống nhúng dựa trên vi điều khiển **ESP8266** nhằm giám sát nồng độ pH và EC theo thời gian thực, đồng thời hỗ trợ cả **hai chế độ vận hành:**
1. **Chế độ tự động (Automatic):** Tự động đo theo chu kỳ, lọc nhiễu tín hiệu bằng thuật toán **Kalman Filter**, tự kích hoạt máy bơm qua Relay để châm thêm dinh dưỡng/chỉnh pH khi thông số vượt ngưỡng, đồng thời gửi thông báo khẩn cấp qua **Discord Webhook** hoặc **Blynk IoT**.
2. **Chế độ bán tự động (Manual / Semi-Automatic):** Cho phép người vận hành quan sát thông số trên Dashboard và chủ động can thiệp bằng **nút bấm tay** trên ứng dụng (Blynk Virtual Pins V1/V2) để kích hoạt bơm/xả nước khi cần thiết.

---

## ⚠️ 2. Lưu ý kỹ thuật & Hiệu chuẩn cảm biến (Calibration Note)

> [!IMPORTANT]
> **Ghi chú về độ chính xác của cảm biến:**  
> Trong quá trình triển khai đồ án ở môi trường học tập, do **chưa có sẵn các bộ dung dịch đệm chuẩn chuyên dụng** (như dung dịch đệm pH 4.01, pH 6.86, pH 9.18 và dung dịch chuẩn EC 1413 µS/cm) để cân chỉnh lại từng điện cực, nên:
> * Hệ số chuyển đổi điện áp sang pH trong code hiện áp dụng theo phương trình Nernst lý thuyết ($59.16\text{ mV/pH}$ ở $25^\circ\text{C}$ và điện áp mốc trung tính $1500\text{ mV}$).
> * Hệ số $K$ của đầu dò EC ($K \approx 9.985$) được thiết lập dựa trên giá trị tham chiếu phần cứng và hiệu chỉnh thực nghiệm tương đối.
> 
> Vì lý do trên, các ngưỡng so sánh và giá trị đo thực tế trong mã nguồn có thể có sai lệch nhất định so với môi trường canh tác tiêu chuẩn thực tế. Khi triển khai thực tế trên diện rộng, hệ thống cần được bổ sung bước căn chỉnh (calibration) với dung dịch chuẩn trước khi đưa vào vận hành lâu dài.

---

## 🛠️ 3. Kiến trúc phần cứng & Sơ đồ đấu nối

Do vi điều khiển ESP8266 chỉ sở hữu **duy nhất 01 chân Analog (A0)**, mạch sử dụng IC dồn kênh analog **CD4051 (8-to-1 Multiplexer)** để đọc luân phiên nhiều cảm biến analog:

```
[ Cảm biến pH ] ──> Kênh CH0 ──┐
                               ├──> [ IC CD4051 ] ──(Chân COM)──> [ Chân A0 - ESP8266 ]
[ Cảm biến EC ] ──> Kênh CH1 ──┘          │
                                          └── (S0, S1, S2) <──── [ Chân D5, D6, D7 ]

[ ESP8266 ] ──(Chân D0)──> [ Relay 1 ] ──> Máy bơm dinh dưỡng / Tăng pH
            ──(Chân D2)──> [ Relay 2 ] ──> Máy bơm dung dịch giảm pH / Nước sạch
```

### Bảng đấu nối chân (Pinout Map):

| Linh kiện | Chân trên linh kiện | Chân trên ESP8266 (NodeMCU) | Chức năng |
| :--- | :--- | :--- | :--- |
| **CD4051** | `S0` | `D5` (GPIO14) | Bit chọn kênh 0 |
| **CD4051** | `S1` | `D6` (GPIO12) | Bit chọn kênh 1 |
| **CD4051** | `S2` | `D7` (GPIO13) | Bit chọn kênh 2 |
| **CD4051** | `COM (Out)` | `A0` (ADC0) | Ngõ ra tín hiệu Analog chung |
| **Cảm biến pH** | Signal Out | `CH0` (Chân 13 - CD4051) | Tín hiệu đo độ pH |
| **Cảm biến EC** | Signal Out | `CH1` (Chân 14 - CD4051) | Tín hiệu đo độ dẫn điện EC |
| **Module Relay 1**| `IN1` | `D0` (GPIO16) | Điều khiển bơm khi pH < 5.5 hoặc EC < 0.8 |
| **Module Relay 2**| `IN2` | `D2` (GPIO4) | Điều khiển bơm khi pH > 7.0 |

---

## 🧠 4. Thuật toán lọc nhiễu Kalman (1D Kalman Filter)

Tín hiệu analog từ cảm biến pH và EC trong môi trường nước có động cơ máy bơm thường xuất hiện nhiều xung nhiễu (noise) do rung động và cảm ứng điện từ. Hệ thống nhúng một bộ lọc Kalman 1 chiều đơn giản trực tiếp trên vi điều khiển:

$$K = \frac{P_{k-1}}{P_{k-1} + R}$$
$$\hat{x}_k = \hat{x}_{k-1} + K \cdot (z_k - \hat{x}_{k-1})$$
$$P_k = (1 - K) \cdot P_{k-1} + | \hat{x}_k - z_k | \cdot Q$$

* **$Q = 0.01$:** Nhiễu quá trình (Process noise covariance).
* **$R = 0.5$:** Nhiễu đo lường (Measurement noise covariance).
* **Hiệu quả:** Làm mượt giá trị pH và EC, triệt tiêu các giá trị đo đột biến sai lệch, tránh hiện tượng Relay đóng ngắt liên tục (chatter).

---

## ⚙️ 5. Logic điều khiển: Tự động kết hợp Bán tự động

### 5.1. Chế độ tự động (Auto Mode)
* **Kích hoạt Relay 1:** Khi `pH < 5.5` hoặc `EC < 0.8 mS/cm` (nước bị chua hoặc thiếu dưỡng chất), hệ thống đóng Relay 1 để bơm dung dịch dinh dưỡng.
* **Kích hoạt Relay 2:** Khi `pH > 7.0` (nước bị kiềm hóa), hệ thống đóng Relay 2 để châm dung dịch giảm pH.
* **Cơ chế giãn thời gian (Sampling rate control):** Khi các thông số đều đạt chuẩn (`5.5 <= pH <= 7.0` và `0.8 <= EC <= 1.5`), hệ thống tự động giãn chu kỳ kiểm tra để tiết kiệm năng lượng và tăng tuổi thọ điện cực; khi có thông số vượt ngưỡng, tần suất kiểm tra và cảnh báo sẽ tăng lên để xử lý kịp thời.
* **Cảnh báo từ xa:** Khi `EC > 1.5 mS/cm`, gửi cảnh báo trực tiếp qua **Discord Webhook** hoặc qua thông báo trên app **Blynk**.

### 5.2. Chế độ bán tự động (Manual Control)
* Trên ứng dụng di động **Blynk**, người dùng có 2 nút nhấn ảo:
  * **Nút bấm V1:** Bật / tắt cưỡng bức máy bơm dinh dưỡng (Relay 1).
  * **Nút bấm V2:** Bật / tắt cưỡng bức máy bơm nước / giảm pH (Relay 2).
* Hỗ trợ người chăm sóc can thiệp nhanh khi quan sát trực tiếp tình trạng cây trồng hoặc khi cần xả/châm bể nước mà không phụ thuộc hoàn toàn vào chu kỳ tự động.

---

## 📁 6. Cấu trúc thư mục

```text
.
├── .gitignore                          # Cấu hình bỏ qua file build và file nhị phân
├── README.md                           # Tài liệu kỹ thuật chi tiết của dự án
│
├── firmware/                           # Mã nguồn Arduino nạp cho ESP8266
│   ├── canh_bao_discord/               # Bản gửi cảnh báo qua Discord Webhook
│   │   └── canh_bao_discord.ino
│   │
│   └── blynk_ban_tu_dong/              # Bản giám sát Dashboard và nút bấm tay qua Blynk IoT
│       └── blynk_ban_tu_dong.ino
│
├── tai_lieu/                           # Slide thuyết trình báo cáo
│   ├── BaoCao_ViDieuKhien.pptx         # Báo cáo tổng kết môn học Vi điều khiển
│   └── BaoCao_IoT.pptx                 # Báo cáo chuyên đề kết nối IoT
│
└── media/                              # Video thực nghiệm
    ├── demo_he_thong.mp4               # Video quay quá trình chạy thực tế của hệ thống
    ├── test_blynk_1.mp4                # Video kiểm thử kết nối Blynk
    └── test_blynk_2.mp4                # Video kiểm thử nút bấm điều khiển bán tự động
```

---

## 🚀 7. Hướng dẫn nạp mã nguồn

1. Cài đặt **Arduino IDE** (phiên bản 1.8.x hoặc 2.x).
2. Thêm bo mạch ESP8266 vào Arduino IDE thông qua `Board Manager`:
   - URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. Cài đặt các thư viện cần thiết từ `Library Manager`:
   - `Blynk` (bởi Volodymyr Shymanskyy)
   - `ESP8266WiFi` và `ESP8266HTTPClient` (đi kèm gói bo mạch ESP8266)
4. Mở file `.ino` trong thư mục `firmware/` tương ứng:
   - Điền tên và mật khẩu WiFi: `ssid` và `password`.
   - Với bản Discord: Điền URL Webhook của kênh Discord vào `discordWebhookURL`.
   - Với bản Blynk: Điền `BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME` và `BLYNK_AUTH_TOKEN`.
5. Chọn Board: **NodeMCU 1.0 (ESP-12E Module)**, cắm cáp Micro-USB và nhấn **Upload**.
