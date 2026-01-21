#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "audio/radio_player.h"
#include "audio/radio_stations.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_nv3023.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#define TAG "FELIXNGUYEN_1_83_1MIC"

// NV3030B 1.83" 240x280 FULL initialization sequence
// Based on ChatGPT research from Waveshare documentation
static const nv3023_lcd_init_cmd_t nv3030b_183_init_cmds[] = {
    // Enter private access mode
    {0xFD, (uint8_t []){0x06, 0x08}, 2, 0},
    
    // Voltage settings
    {0x61, (uint8_t []){0x07, 0x04}, 2, 0},
    {0x62, (uint8_t []){0x00, 0x44, 0x45}, 3, 0},
    {0x63, (uint8_t []){0x41, 0x07, 0x12, 0x12}, 4, 0},
    {0x64, (uint8_t []){0x37}, 1, 0},
    {0x65, (uint8_t []){0x09, 0x10, 0x21}, 3, 0},
    {0x66, (uint8_t []){0x09, 0x10, 0x21}, 3, 0},
    {0x67, (uint8_t []){0x20, 0x40}, 2, 0},
    {0x68, (uint8_t []){0x90, 0x4C, 0x7C, 0x66}, 4, 0},
    
    // Timing settings
    {0xB1, (uint8_t []){0x0F, 0x08, 0x01}, 3, 0},
    {0xB4, (uint8_t []){0x01}, 1, 0},
    {0xB5, (uint8_t []){0x02, 0x02, 0x0A, 0x14}, 4, 0},
    {0xB6, (uint8_t []){0x04, 0x01, 0x9F, 0x00, 0x02}, 5, 0},
    {0xDF, (uint8_t []){0x11}, 1, 0},
    
    // Gamma settings (critical for display quality!)
    {0xE2, (uint8_t []){0x13, 0x00, 0x00, 0x30, 0x33, 0x3F}, 6, 0},
    {0xE5, (uint8_t []){0x3F, 0x33, 0x30, 0x00, 0x00, 0x13}, 6, 0},
    {0xE1, (uint8_t []){0x00, 0x57}, 2, 0},
    {0xE4, (uint8_t []){0x58, 0x00}, 2, 0},
    {0xE0, (uint8_t []){0x01, 0x03, 0x0D, 0x0E, 0x0E, 0x0C, 0x15, 0x19}, 8, 0},
    {0xE3, (uint8_t []){0x1A, 0x16, 0x0C, 0x0F, 0x0E, 0x0D, 0x02, 0x01}, 8, 0},
    {0xE6, (uint8_t []){0x00, 0xFF}, 2, 0},
    {0xE7, (uint8_t []){0x01, 0x04, 0x03, 0x03, 0x00, 0x12}, 6, 0},
    {0xE8, (uint8_t []){0x00, 0x70, 0x00}, 3, 0},
    {0xEC, (uint8_t []){0x52}, 1, 0},
    {0xF1, (uint8_t []){0x01, 0x01, 0x02}, 3, 0},
    {0xF6, (uint8_t []){0x09, 0x10, 0x00, 0x00}, 4, 0},
    
    // Exit private access mode
    {0xFD, (uint8_t []){0xFA, 0xFC}, 2, 0},
    
    // Set COLMOD to RGB565 (16-bit)
    {0x3A, (uint8_t []){0x05}, 1, 0},
    
    // TE OFF (tearing effect)
    {0x35, (uint8_t []){0x00}, 1, 0},
    
    // Display inversion ON (important for correct colors)
    {0x21, (uint8_t []){0}, 0, 0},
    
    // Sleep out (with delay)
    {0x11, (uint8_t []){0}, 0, 200},
    
    // Display ON
    {0x29, (uint8_t []){0}, 0, 10},
};

class FelixNguyenLcd183Board : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    SpiLcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_21);
        rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_21, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_21, 0);
            rtc_gpio_hold_en(GPIO_NUM_21);
            esp_lcd_panel_disp_on_off(panel_, false);
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_HEIGHT * 80 * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeNv3030bDisplay() {
        ESP_LOGI(TAG, "Install NV3030B panel IO (SPI mode 0, 40MHz)");
        esp_lcd_panel_io_spi_config_t io_config = NV3023_PANEL_IO_SPI_CONFIG(DISPLAY_CS, DISPLAY_DC, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install NV3030B LCD driver with FULL 1.83 inch init sequence");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR; // BGR order per Ghidra analysis
        panel_config.bits_per_pixel = 16;
        
        // Use COMPLETE init commands for 1.83" NV3030B display
        nv3023_vendor_config_t vendor_config = {
            .init_cmds = nv3030b_183_init_cmds,
            .init_cmds_size = sizeof(nv3030b_183_init_cmds) / sizeof(nv3023_lcd_init_cmd_t),
        };
        panel_config.vendor_config = &vendor_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_nv3023(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        
        // Set LCD RAM gap/offset
        // Math: RAM(320) - Glass(284) = 36. This aligns to the start of the glass.
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_, 36, 0));
        
        // NOTE: Inversion already set in init sequence (0x21), don't call again!
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new SpiLcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, 
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    FelixNguyenLcd183Board() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeNv3030bDisplay();
        GetBacklight()->RestoreBrightness();
        
        // Initialize radio player and register MCP tools
        RadioPlayer::GetInstance().Initialize(GetAudioCodec());
        RegisterRadioTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        level = 100;
        charging = false;
        discharging = true;
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

private:
    void RegisterRadioTools() {
        auto& mcp = McpServer::GetInstance();
        auto& db = RadioStationsDB::GetInstance();
        
        // Radio playback tool
        mcp.AddTool("self.audio.play_radio",
            db.GetStationListDescription(),
            PropertyList({
                Property("station", kPropertyTypeString, "Station ID (e.g., vov1, voh)")
            }),
            [](const PropertyList& props) -> ReturnValue {
                auto id = props["station"].value<std::string>();
                bool success = RadioPlayer::GetInstance().Play(id);
                return success;
            });
        
        // Stop audio tool
        mcp.AddTool("self.audio.stop",
            "Stop current audio playback (radio or music)",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                RadioPlayer::GetInstance().Stop();
                return true;
            });
    }
};

DECLARE_BOARD(FelixNguyenLcd183Board);
