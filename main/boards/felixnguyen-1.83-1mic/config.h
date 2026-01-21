#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ===== FelixNguyen 1.83 Inch 1 Mic =====
// Display: NV3030B/ST7789P, 284x240, SPI

// Audio I2S Configuration - 1 Mic version
#define AUDIO_INPUT_SAMPLE_RATE  16000
// Standardize on 16000Hz to match Input and TTS
// Previous 24000Hz setting caused pitch/speed issues
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// Microphone Input (INMP441 or similar PDM/I2S mic)
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

// Speaker Output (I2S DAC)
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

// Buttons
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_39
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_40

// Display Configuration - NV3030B/ST7789P
#define DISPLAY_SDA             GPIO_NUM_10  // MOSI
#define DISPLAY_SCL             GPIO_NUM_9   // CLK
#define DISPLAY_DC              GPIO_NUM_8
#define DISPLAY_CS              GPIO_NUM_14
#define DISPLAY_RES             GPIO_NUM_18
#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_13
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Resolution 280x240 (Landscape with swap_xy)
#define DISPLAY_WIDTH           280
#define DISPLAY_HEIGHT          240
#define DISPLAY_SWAP_XY         true
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        true   // 180° rotation
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        20  // Common offset for NV3030B 1.83 inch

// Power Management
#define BATTERY_ADC_PIN         GPIO_NUM_38
#define SLEEP_CTRL_PIN          GPIO_NUM_21

#endif // _BOARD_CONFIG_H_
