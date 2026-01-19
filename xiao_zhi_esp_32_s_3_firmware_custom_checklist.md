# XiaoZhi ESP32-S3 – Firmware Custom Checklist (DETAILED)

> Mục tiêu: build + flash lại firmware XiaoZhi ESP32-S3 từ **SOURCE CHÍNH THỨC**, có thể custom UI, font tiếng Việt, logic xử lý, **KHÔNG dùng firmware đóng**.
>
> Nguyên tắc an toàn:
>
> - Không sửa bootloader nếu không cần
> - Luôn backup flash trước khi ghi
> - Test bằng USB trước, chưa đụng OTA

---

## PHASE 0 – XÁC NHẬN PHẦN CỨNG (BẮT BUỘC)

> ✅ **ĐÃ XÁC NHẬN THỰC TẾ TỪ THIẾT BỊ CỦA PHÚC** (log esptool)

### ☑ 0.1 Thông tin chip (CONFIRMED)

```text
Chip type:          ESP32-S3 (QFN56)
Revision:           v0.2
CPU:                Dual Core + LP Core @ 240MHz
Wireless:           Wi-Fi + Bluetooth 5 (LE)
Crystal:            40MHz
USB mode:           USB-Serial/JTAG
```

➡️ **Kết luận**:

- Target ESP-IDF: `esp32s3`
- Không phải ESP32 / ESP32-C3 → chọn đúng target

---

### ☑ 0.2 Bộ nhớ (CONFIRMED)

```text
Embedded PSRAM:     8MB (AP_3v3)
Flash:              (chưa dump, nhưng XiaoZhi S3 thường ≥ 8MB)
```

➡️ **Kết luận quan trọng**:

- Có thể dùng:
  - LVGL
  - Font Unicode lớn (tiếng Việt full)
  - Buffer audio / TTS
- **BẮT BUỘC enable PSRAM trong sdkconfig**

---

### ☑ 0.3 Kết nối & Flash mode

```text
Port:               /dev/cu.usbmodem101
USB:                Native USB (CDC)
MAC:                90:70:69:1b:b5:9c
```

➡️ **Kết luận**:

- Không cần UART ngoài
- Flash trực tiếp qua USB-C
- Tool dùng: `esptool >= 5.1`

---

### ☐ 0.4 Backup flash (CHƯA LÀM – BẮT BUỘC TRƯỚC KHI FLASH)

```bash
python3 -m esptool --port /dev/cu.usbmodem101 read_flash 0 0x1000000 backup_xiaozhi_s3.bin
```

⚠️ **KHÔNG ĐƯỢC FLASH nếu chưa có file backup**

---



## PHASE 1 – CLONE SOURCE CHÍNH THỨC

### ☐ 1.1 Clone repo XiaoZhi ESP32 (official)

```bash
git clone https://github.com/xiaozhi-ai/xiaozhi-esp32.git
cd xiaozhi-esp32
```

### ☐ 1.2 Checkout nhánh stable

```bash
git branch -a
git checkout main
```

### ☐ 1.3 Đọc nhanh cấu trúc repo

Bắt buộc xem:

- `main/`
- `components/`
- `sdkconfig.defaults`
- `partitions.csv`

---

## PHASE 2 – SETUP ENV ESP-IDF (ESP32-S3)

### ☐ 2.1 Cài ESP-IDF (>= v5.1)

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.1 --recursive https://github.com/espressif/esp-idf.git
./esp-idf/install.sh esp32s3
. ./esp-idf/export.sh
```

### ☐ 2.2 Set target

```bash
idf.py set-target esp32s3
```

### ☐ 2.3 Build thử (chưa sửa gì)

```bash
idf.py build
```

⚠️ Nếu FAIL → chưa được sửa code

---

## PHASE 3 – CẤU HÌNH MÀN HÌNH (LVGL / TFT)

### ☐ 3.1 Tìm module display

Thường nằm tại:

- `components/display/`
- hoặc `components/lvgl_port/`

### ☐ 3.2 Kiểm tra driver

Xác nhận có dòng tương tự:

```c
#define LCD_H_RES 240
#define LCD_V_RES 284
```

### ☐ 3.3 Fix rotation (nếu bị xoay)

Trong init LCD:

```c
lcd_panel_set_rotation(panel, ROTATE_90);
```

---

## PHASE 4 – THÊM FONT TIẾNG VIỆT (CỐT LÕI)

> **ĐÚNG**: Firmware tiếng Việt ≠ logic mới 👉 90% chỉ là **FONT UTF-8 + LVGL config**

### ☐ 4.1 Chuẩn bị font

- Dùng `lv_font_conv`
- Font gợi ý:
  - NotoSans-Regular.ttf

Command:

```bash
lv_font_conv \
--font NotoSans-Regular.ttf \
--size 16 \
--bpp 4 \
--format lvgl \
--symbols "ÀÁÂÃÈÉÊÌÍÒÓÔÕÙÚĂĐĨŨƠàáâãèéêìíòóôõùúăđĩũơƯưẠ-ỹ" \
-o font_vi_16.c
```

### ☐ 4.2 Add vào project

- Copy `font_vi_16.c` vào `components/ui/fonts/`
- Khai báo trong:

```c
LV_FONT_DECLARE(font_vi_16);
```

### ☐ 4.3 Set font mặc định

```c
style.text.font = &font_vi_16;
```

---

## PHASE 5 – UTF-8 & TEXT PIPELINE

### ☐ 5.1 Đảm bảo string là UTF-8

- KHÔNG dùng `char*` ascii cứng
- Dùng string từ cloud 그대로

### ☐ 5.2 LVGL config

Trong `sdkconfig`:

```text
CONFIG_LV_TXT_ENC_UTF8=y
```

---

## PHASE 6 – BUILD & FLASH AN TOÀN

### ☐ 6.1 Backup flash (BẮT BUỘC)

```bash
esptool.py read_flash 0 0x1000000 backup.bin
```

### ☐ 6.2 Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### ☐ 6.3 Test

Checklist test:

-

---

## PHASE 7 – CUSTOM LOGIC (SAU KHI OK)

### ☐ 7.1 UI

- Sửa layout LVGL
- Thêm trạng thái: `thinking`, `waiting`, `speaking`

### ☐ 7.2 Chuẩn bị async (Jarvis)

- Chưa cần sửa firmware timeout
- Firmware chỉ cần:
  - Hiển thị "Đang xử lý…"
  - Chờ response

---

## PHASE 8 – NHỮNG THỨ TUYỆT ĐỐI KHÔNG LÀM

- ❌ Không flash firmware bán sẵn lại
- ❌ Không đổi partition khi chưa hiểu
- ❌ Không optimize trước khi chạy ổn

---

## DONE CRITERIA

✔ Build clean ✔ Flash không lỗi ✔ Hiển thị tiếng Việt chuẩn ✔ Có source để custom lâu dài

---

> Nếu dùng AI Vibe Code: "Follow this checklist strictly, stop before flashing if any step fails."

