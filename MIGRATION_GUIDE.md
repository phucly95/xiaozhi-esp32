# Hướng dẫn Clone và Setup trên macOS

Để tiếp tục phát triển trên macOS, bạn hãy làm theo các bước sau:

## 1. Clone Repository

Mở Terminal và chạy lệnh sau để clone repo từ fork của bạn:

```bash
git clone https://github.com/phucly95/xiaozhi-esp32.git
cd xiaozhi-esp32
```

## 2. Checkout Branch Làm Việc

Chuyển sang branch bạn đang phát triển (`feature/custom-xiaozhi-v1.6.6`):

```bash
git checkout feature/custom-xiaozhi-v1.6.6
```

## 3. Cài đặt Môi trường ESP-IDF

Đảm bảo bạn đã cài đặt ESP-IDF. Nếu chưa, hãy làm theo hướng dẫn chính thức của Espressif. Sau đó export các biến môi trường:

```bash
. $HOME/esp/esp-idf/export.sh
```

## 4. Cấu hình và Build

### Cài đặt Target

```bash
idf.py set-target esp32s3
```

### Khôi phục các file cấu hình (nếu cần)

Các file sau nằm trong `.gitignore` và sẽ không được clone về. Bạn cần kiểm tra và copy thủ công nếu có thay đổi quan trọng, hoặc để project tự tạo lại file mặc định:

*   `sdkconfig`: File cấu hình build. Bạn có thể copy từ máy Windows sang hoặc chạy `idf.py menuconfig` để cấu hình lại.
*   `main/assets/lang_config.h`: File cấu hình ngôn ngữ (nếu có custom).
*   `.env`: Biến môi trường (nếu có).

### Build Project

Lệnh build sẽ tự động tải các `managed_components` cần thiết:

```bash
idf.py build
```

## 5. Flash và Monitor

Kết nối board ESP32-S3 với Mac và chạy:

```bash
idf.py -p /dev/tty.usbmodem... flash monitor
```
(Thay `/dev/tty.usbmodem...` bằng cổng serial thực tế của bạn).
