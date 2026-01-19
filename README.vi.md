# Hướng dẫn Custom Firmware XiaoZhi (Board FelixNguyen)

Tài liệu này ghi lại các bước đã thực hiện để tùy biến firmware XiaoZhi cho board mạch **FelixNguyen** (ESP32-S3 + Màn hình 1.83" ST7789/NV3030B).

## 1. Cấu hình Màn hình (Display Fix)

Màn hình gốc bị lệch và hiển thị sai màu. Đã khắc phục bằng cách cấu hình lại driver và offset.

*   **Driver**: Sử dụng cấu hình tương thích `NV3023` (trong code thực tế là `st7789` variant).
*   **Độ phân giải**: `280x240` (Landscape).
*   **Offset (Gap)**:
    *   Xác định thực tế: `x_gap = 38`.
    *   Cấu hình trong file: `main/boards/felixnguyen-1.83-1mic/felixnguyen-1.83-1mic.cc`.
    *   Hàm gọi: `esp_lcd_panel_set_gap(panel_handle, 38, 0);`.

## 2. Hỗ trợ Tiếng Việt (Font Full Unicode)

Hệ thống mặc định không hỗ trợ đầy đủ tiếng Việt (thiếu các ký tự ơ, ư, ă...). Đã cập nhật quy trình tạo font mới.

### Cách tạo Font:
Script tự động: `scripts/generate_roboto_font.py`

Script này sẽ gọi công cụ `lv_font_conv` (được cài đặt local trong thư mục `tools/`) để tạo ra các file `.c` cho LVGL. Điểm quan trọng là sử dụng tham số `--range` để bao gồm tất cả các dải Unicode tiếng Việt:

*   `0x20-0x7F`: Ký tự cơ bản (Basic Latin).
*   `0xA0-0xFF`: Ký tự Latin bổ sung (Latin-1 Supplement).
*   `0x100-0x24F`: Latin mở rộng A & B (Bao gồm **Ă, Đ, Ơ, Ư**).
*   `0x300-0x36F`: Dấu phụ kết hợp (Combining Diacritical Marks).
*   `0x1E00-0x1EFF`: Latin mở rộng bổ sung (Các ký tự có dấu chồng như **ạ, ả, ấ...**).
*   `0x20AB`: Ký hiệu tiền tệ (₫).

**Lệnh thực hiện:**
```bash
# Đảm bảo đã cài dependencies
cd tools
npm install

# Quay lại root và chạy script
cd ..
python scripts/generate_roboto_font.py
```

Sau khi chạy, các file font mới sẽ nằm trong `managed_components/78__xiaozhi-fonts/src/`.

### Thay đổi cấu hình chung:
*   Đã enable tiếng Việt trong `sdkconfig`: `CONFIG_LANGUAGE_VI_VN=y`.
*   Đã set font mặc định khởi động là `font_roboto_20_4` trong `main/CMakeLists.txt`.

## 3. Cấu hình Wake Word (Từ khóa đánh thức)

Đã thay đổi từ khóa mặc định từ "Nihao Xiaozhi" sang **"Hey, Lily"**.

**Cấu hình trong `sdkconfig`:**
```ini
# Tắt mặc định
CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=n
# Bật Hey Lily
CONFIG_SR_WN_WN9_HILILI_TTS=y
```

## 4. Hướng dẫn Build & Flash

Sử dụng môi trường ESP-IDF v5.x.

**Bước 1: Clean (nếu cần thiết)**
Nếu gặp lỗi path python hoặc lỗi lạ, hãy run lệnh này trước:
```bash
idf.py fullclean
```

**Bước 2: Build Firmware**
```bash
idf.py build
```

**Bước 3: Flash xuống thiết bị**
Kết nối thiết bị qua USB (Cổng UART/COM).
```bash
idf.py -p COM3 flash
# Thay COM3 bằng cổng thực tế của bạn
```

**Bước 4: Monitor (Xem log)**
```bash
idf.py monitor
```
(Thoát monitor bằng phím `Ctrl + ]`)

## 5. Cấu trúc thư mục quan trọng
*   `main/boards/felixnguyen-1.83-1mic/`: Chứa code cấu hình board và màn hình.
*   `main/assets/locales/vi-VN/`: File ngôn ngữ `language.json`.
*   `scripts/`: Các script tiện ích (`generate_roboto_font.py`, `gen_lang.py`).
*   `tools/`: Chứa `lv_font_conv` cục bộ để build font.
