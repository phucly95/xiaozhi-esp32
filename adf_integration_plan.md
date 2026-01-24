# Plan Tích Hợp ESP-ADF v2.7 vào Xiaozhi ESP32-S3

## ✅ Environment Verified
- ESP-IDF: v5.3.0 ✅
- ESP-ADF: v2.7 ✅
- Board: ESP32-S3 với 8MB PSRAM ✅

## 🎯 Objectives
1. Internet Radio streaming (MP3, AAC, HLS/m3u8)
2. YouTube audio playback (via backend server)
3. Multi-codec auto-detection
4. Seamless integration với xiaozhi architecture hiện tại

---

## 📋 Implementation Plan

### **Phase 1: Project Configuration (30 phút)**

#### Step 1.1: Modify Root CMakeLists.txt

**File: `CMakeLists.txt` (project root)**

Thêm **TRƯỚC** dòng `project(xiaozhi-esp32)`:

```cmake
cmake_minimum_required(VERSION 3.16)

# ========== ESP-ADF Integration ==========
if(DEFINED ENV{ADF_PATH})
    set(ADF_PATH $ENV{ADF_PATH})
    list(APPEND EXTRA_COMPONENT_DIRS ${ADF_PATH}/components)
    message(STATUS "ADF_PATH: ${ADF_PATH}")
else()
    message(FATAL_ERROR "ADF_PATH not set. Please: export ADF_PATH=~/esp/esp-adf")
endif()
# ==========================================

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi-esp32)
```

#### Step 1.2: Verify ADF_PATH

```bash
# Check environment variable
echo $ADF_PATH
# Should print: /home/your_user/esp/esp-adf (or similar)

# If not set:
export ADF_PATH=~/esp/esp-adf
echo 'export ADF_PATH=~/esp/esp-adf' >> ~/.bashrc
```

#### Step 1.3: Test Build with ADF

```bash
cd /path/to/xiaozhi-esp32
idf.py reconfigure
# Should see: "ADF_PATH: /home/.../esp-adf"
```

---

### **Phase 2: Add ADF Components (1 giờ)**

#### Step 2.1: Update main/CMakeLists.txt

**File: `main/CMakeLists.txt`**

Tìm dòng `idf_component_register(` và thêm ADF components vào `REQUIRES`:

```cmake
idf_component_register(
    SRCS ${SOURCES}
    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES 
        # Existing requirements (giữ nguyên tất cả)
        
        # ===== ADF Components =====
        audio_pipeline
        audio_element
        audio_stream
        http_stream
        i2s_stream
        mp3_decoder
        aac_decoder
        audio_sal
        esp_peripherals
        # ==========================
        
    WHOLE_ARCHIVE
)
```

#### Step 2.2: Add ADF Radio Source Files

Thêm vào `set(SOURCES ...` section:

```cmake
set(SOURCES 
    "audio/audio_codec.cc"
    "audio/audio_service.cc"
    "audio/radio_player.cc"
    "audio/adf_radio_player.cc"  # ← NEW FILE
    "audio/ring_buffer.cc"
    # ... rest of existing files ...
)
```

---

### **Phase 3: Create ADF Radio Player (2-3 giờ)**

#### Step 3.1: Create Header File

**Tạo file mới: `main/audio/adf_radio_player.h`**

```cpp
#pragma once

#include <string>
#include <memory>

// ADF headers
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "aac_decoder.h"

#include "audio_codec.h"
#include "radio_stations.h"

/**
 * ADF-based Radio Player for Xiaozhi
 * 
 * Features:
 * - Multi-codec support (MP3, AAC auto-detection)
 * - HLS playlist parser (m3u8)
 * - HTTP/HTTPS streaming
 * - Integration with xiaozhi AudioCodec
 */
class AdfRadioPlayer {
public:
    enum class State {
        IDLE,
        CONNECTING,
        PLAYING,
        PAUSED,
        ERROR
    };

    static AdfRadioPlayer& GetInstance();

    bool Initialize(AudioCodec* codec);
    bool Play(const std::string& station_id);
    bool PlayURL(const std::string& url);
    void Stop();
    void Pause();
    void Resume();
    void SetVolume(int volume); // 0-100
    
    State GetState() const { return state_; }
    const RadioStation* GetCurrentStation() const { return current_station_; }
    bool IsPlaying() const { return state_ == State::PLAYING; }

private:
    AdfRadioPlayer();
    ~AdfRadioPlayer();
    
    AdfRadioPlayer(const AdfRadioPlayer&) = delete;
    AdfRadioPlayer& operator=(const AdfRadioPlayer&) = delete;

    // Pipeline management
    bool CreatePipeline(const std::string& url);
    void DestroyPipeline();
    
    // Codec detection from URL
    const char* DetectCodecFromURL(const std::string& url);
    
    // Event handling
    static void EventTask(void* param);
    void ProcessEvents();
    
    State state_;
    const RadioStation* current_station_;
    AudioCodec* codec_;
    
    // ADF components
    audio_pipeline_handle_t pipeline_;
    audio_element_handle_t http_stream_;
    audio_element_handle_t decoder_;
    audio_element_handle_t i2s_stream_;
    audio_event_iface_handle_t evt_;
    
    TaskHandle_t event_task_;
    bool should_stop_;
    int volume_;
    std::string current_url_;
};
```

#### Step 3.2: Create Implementation File

**Tạo file mới: `main/audio/adf_radio_player.cc`**

```cpp
#include "adf_radio_player.h"
#include <esp_log.h>
#include <string.h>
#include "board.h"

#define TAG "ADF_Radio"

AdfRadioPlayer& AdfRadioPlayer::GetInstance() {
    static AdfRadioPlayer instance;
    return instance;
}

AdfRadioPlayer::AdfRadioPlayer()
    : state_(State::IDLE)
    , current_station_(nullptr)
    , codec_(nullptr)
    , pipeline_(nullptr)
    , http_stream_(nullptr)
    , decoder_(nullptr)
    , i2s_stream_(nullptr)
    , evt_(nullptr)
    , event_task_(nullptr)
    , should_stop_(false)
    , volume_(70)
{
}

AdfRadioPlayer::~AdfRadioPlayer() {
    Stop();
    if (evt_) {
        audio_event_iface_destroy(evt_);
    }
}

bool AdfRadioPlayer::Initialize(AudioCodec* codec) {
    ESP_LOGI(TAG, "Initializing ADF Radio Player v2.7");
    
    codec_ = codec;
    
    // Create event interface
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt_ = audio_event_iface_init(&evt_cfg);
    
    if (!evt_) {
        ESP_LOGE(TAG, "Failed to create event interface");
        return false;
    }
    
    ESP_LOGI(TAG, "ADF Radio Player initialized");
    return true;
}

const char* AdfRadioPlayer::DetectCodecFromURL(const std::string& url) {
    // Check file extension in URL
    if (url.find(".mp3") != std::string::npos) {
        return "mp3";
    } else if (url.find(".aac") != std::string::npos || 
               url.find(".m4a") != std::string::npos) {
        return "aac";
    } else if (url.find("aac") != std::string::npos) {
        // Some streams have "aac" in path but no extension
        return "aac";
    }
    
    // Default to MP3 for internet radio streams
    return "mp3";
}

bool AdfRadioPlayer::CreatePipeline(const std::string& url) {
    ESP_LOGI(TAG, "Creating pipeline for: %s", url.c_str());
    
    // 1. Create pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_ = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline_) {
        ESP_LOGE(TAG, "Failed to create pipeline");
        return false;
    }
    
    // 2. HTTP Stream element
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = evt_;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;  // Enable HLS/m3u8 parsing
    http_cfg.task_stack = 4096;
    http_cfg.task_core = 0;
    http_cfg.task_prio = 5;
    
    http_stream_ = http_stream_init(&http_cfg);
    if (!http_stream_) {
        ESP_LOGE(TAG, "Failed to create HTTP stream");
        audio_pipeline_deinit(pipeline_);
        pipeline_ = nullptr;
        return false;
    }
    
    // 3. Decoder element (auto-detect)
    const char* codec_type = DetectCodecFromURL(url);
    ESP_LOGI(TAG, "Using codec: %s", codec_type);
    
    if (strcmp(codec_type, "aac") == 0) {
        aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
        decoder_ = aac_decoder_init(&aac_cfg);
    } else {
        // Default: MP3
        mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
        decoder_ = mp3_decoder_init(&mp3_cfg);
    }
    
    if (!decoder_) {
        ESP_LOGE(TAG, "Failed to create decoder");
        audio_element_deinit(http_stream_);
        audio_pipeline_deinit(pipeline_);
        http_stream_ = nullptr;
        pipeline_ = nullptr;
        return false;
    }
    
    // 4. I2S Stream element
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.task_stack = 4096;
    i2s_cfg.task_core = 1;
    i2s_cfg.task_prio = 23;
    
    // Important: Use existing I2S instance from AudioCodec
    i2s_cfg.i2s_port = (i2s_port_t)0;  // I2S_NUM_0
    i2s_cfg.use_alc = false;
    i2s_cfg.volume = volume_;
    
    i2s_stream_ = i2s_stream_init(&i2s_cfg);
    if (!i2s_stream_) {
        ESP_LOGE(TAG, "Failed to create I2S stream");
        audio_element_deinit(decoder_);
        audio_element_deinit(http_stream_);
        audio_pipeline_deinit(pipeline_);
        decoder_ = nullptr;
        http_stream_ = nullptr;
        pipeline_ = nullptr;
        return false;
    }
    
    // 5. Register all elements
    audio_pipeline_register(pipeline_, http_stream_, "http");
    audio_pipeline_register(pipeline_, decoder_, "dec");
    audio_pipeline_register(pipeline_, i2s_stream_, "i2s");
    
    // 6. Link elements: http → decoder → i2s
    const char *link_tag[3] = {"http", "dec", "i2s"};
    audio_pipeline_link(pipeline_, &link_tag[0], 3);
    
    // 7. Set up event listener
    audio_pipeline_set_listener(pipeline_, evt_);
    
    // 8. Set URI
    audio_element_set_uri(http_stream_, url.c_str());
    
    ESP_LOGI(TAG, "Pipeline created successfully");
    return true;
}

void AdfRadioPlayer::DestroyPipeline() {
    if (pipeline_) {
        audio_pipeline_stop(pipeline_);
        audio_pipeline_wait_for_stop(pipeline_);
        audio_pipeline_terminate(pipeline_);
        
        audio_pipeline_unregister(pipeline_, http_stream_);
        audio_pipeline_unregister(pipeline_, decoder_);
        audio_pipeline_unregister(pipeline_, i2s_stream_);
        
        audio_pipeline_remove_listener(pipeline_);
        
        audio_pipeline_deinit(pipeline_);
        pipeline_ = nullptr;
    }
    
    if (http_stream_) {
        audio_element_deinit(http_stream_);
        http_stream_ = nullptr;
    }
    
    if (decoder_) {
        audio_element_deinit(decoder_);
        decoder_ = nullptr;
    }
    
    if (i2s_stream_) {
        audio_element_deinit(i2s_stream_);
        i2s_stream_ = nullptr;
    }
}

bool AdfRadioPlayer::Play(const std::string& station_id) {
    auto& db = RadioStationsDB::GetInstance();
    auto station = db.GetStation(station_id);
    
    if (!station) {
        ESP_LOGE(TAG, "Station not found: %s", station_id.c_str());
        return false;
    }
    
    current_station_ = station;
    ESP_LOGI(TAG, "Playing station: %s", station->name.c_str());
    
    return PlayURL(station->url);
}

bool AdfRadioPlayer::PlayURL(const std::string& url) {
    Stop();
    
    current_url_ = url;
    state_ = State::CONNECTING;
    
    // Create and setup pipeline
    if (!CreatePipeline(url)) {
        state_ = State::ERROR;
        return false;
    }
    
    // Start pipeline
    audio_pipeline_run(pipeline_);
    
    // Create event monitoring task
    should_stop_ = false;
    xTaskCreatePinnedToCore(EventTask, "adf_evt", 4096, this, 5, &event_task_, 0);
    
    state_ = State::PLAYING;
    ESP_LOGI(TAG, "Playback started");
    return true;
}

void AdfRadioPlayer::Stop() {
    if (state_ == State::IDLE) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping playback");
    
    should_stop_ = true;
    state_ = State::IDLE;
    
    // Wait for event task
    if (event_task_) {
        vTaskDelay(pdMS_TO_TICKS(200));
        event_task_ = nullptr;
    }
    
    DestroyPipeline();
    current_station_ = nullptr;
    current_url_.clear();
}

void AdfRadioPlayer::Pause() {
    if (state_ == State::PLAYING && pipeline_) {
        audio_pipeline_pause(pipeline_);
        state_ = State::PAUSED;
        ESP_LOGI(TAG, "Paused");
    }
}

void AdfRadioPlayer::Resume() {
    if (state_ == State::PAUSED && pipeline_) {
        audio_pipeline_resume(pipeline_);
        state_ = State::PLAYING;
        ESP_LOGI(TAG, "Resumed");
    }
}

void AdfRadioPlayer::SetVolume(int volume) {
    volume_ = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    
    // Apply to I2S stream if active
    if (i2s_stream_) {
        i2s_stream_set_volume(i2s_stream_, volume_);
    }
    
    ESP_LOGI(TAG, "Volume set to: %d", volume_);
}

void AdfRadioPlayer::EventTask(void* param) {
    AdfRadioPlayer* player = static_cast<AdfRadioPlayer*>(param);
    player->ProcessEvents();
    vTaskDelete(nullptr);
}

void AdfRadioPlayer::ProcessEvents() {
    ESP_LOGI(TAG, "Event task started");
    
    while (!should_stop_) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt_, &msg, pdMS_TO_TICKS(100));
        
        if (ret != ESP_OK) {
            continue;
        }
        
        // Log all events for debugging
        ESP_LOGD(TAG, "Event: src=%d, cmd=%d, data=%d", 
                 msg.source_type, msg.cmd, (int)msg.data);
        
        // Handle pipeline events
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                audio_element_status_t status = (audio_element_status_t)msg.data;
                
                switch (status) {
                case AEL_STATUS_STATE_RUNNING:
                    ESP_LOGI(TAG, "[ * ] Element running");
                    if (state_ == State::CONNECTING) {
                        state_ = State::PLAYING;
                    }
                    break;
                    
                case AEL_STATUS_STATE_STOPPED:
                    ESP_LOGI(TAG, "[ * ] Element stopped");
                    break;
                    
                case AEL_STATUS_ERROR_OPEN:
                    ESP_LOGE(TAG, "[ * ] Error opening stream");
                    state_ = State::ERROR;
                    break;
                    
                case AEL_STATUS_ERROR_PROCESS:
                    ESP_LOGE(TAG, "[ * ] Error processing");
                    break;
                    
                case AEL_STATUS_STATE_FINISHED:
                    ESP_LOGI(TAG, "[ * ] Stream finished");
                    break;
                    
                default:
                    break;
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Event task ended");
}
```

---

### **Phase 4: Integration với MCP Server (30 phút)**

#### Step 4.1: Update mcp_server.cc

**File: `main/mcp_server.cc`**

Thêm include:
```cpp
#include "audio/adf_radio_player.h"
```

Trong hàm `AddCommonTools()`, thêm:

```cpp
void McpServer::AddCommonTools() {
    // ... existing tools ...
    
    // ===== Radio Tools (ADF-based) =====
    auto& radio = AdfRadioPlayer::GetInstance();
    
    AddTool("self.radio.play",
        "Play internet radio station. " + 
        RadioStationsDB::GetInstance().GetStationListDescription(),
        PropertyList({
            Property("station_id", kPropertyTypeString)
        }),
        [&radio](const PropertyList& properties) -> ReturnValue {
            std::string station_id = properties["station_id"].value<std::string>();
            bool success = radio.Play(station_id);
            
            if (success) {
                auto station = radio.GetCurrentStation();
                return std::string("Playing: ") + (station ? station->name : "Unknown");
            }
            return "Failed to play station";
        });
    
    AddTool("self.radio.stop",
        "Stop radio playback",
        PropertyList(),
        [&radio](const PropertyList& properties) -> ReturnValue {
            radio.Stop();
            return "Radio stopped";
        });
    
    AddTool("self.radio.set_volume",
        "Set radio volume (0-100)",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }),
        [&radio](const PropertyList& properties) -> ReturnValue {
            int vol = properties["volume"].value<int>();
            radio.SetVolume(vol);
            return "Volume set to " + std::to_string(vol);
        });
    
    AddTool("self.radio.get_status",
        "Get current radio status",
        PropertyList(),
        [&radio](const PropertyList& properties) -> ReturnValue {
            cJSON* json = cJSON_CreateObject();
            
            auto state = radio.GetState();
            const char* state_str = "idle";
            if (state == AdfRadioPlayer::State::PLAYING) state_str = "playing";
            else if (state == AdfRadioPlayer::State::PAUSED) state_str = "paused";
            else if (state == AdfRadioPlayer::State::CONNECTING) state_str = "connecting";
            else if (state == AdfRadioPlayer::State::ERROR) state_str = "error";
            
            cJSON_AddStringToObject(json, "state", state_str);
            
            auto station = radio.GetCurrentStation();
            if (station) {
                cJSON* station_json = cJSON_CreateObject();
                cJSON_AddStringToObject(station_json, "id", station->id.c_str());
                cJSON_AddStringToObject(station_json, "name", station->name.c_str());
                cJSON_AddStringToObject(station_json, "genre", station->genre.c_str());
                cJSON_AddItemToObject(json, "station", station_json);
            }
            
            return json;
        });
}
```

#### Step 4.2: Initialize trong Application

**File: `main/application.cc`**

Thêm include:
```cpp
#include "audio/adf_radio_player.h"
```

Trong `Application::Initialize()`:
```cpp
void Application::Initialize() {
    // ... existing initialization ...
    
    // Initialize ADF Radio Player
    auto& radio = AdfRadioPlayer::GetInstance();
    if (radio.Initialize(board_.GetAudioCodec())) {
        ESP_LOGI(TAG, "ADF Radio player initialized");
    } else {
        ESP_LOGW(TAG, "Failed to initialize radio player");
    }
}
```

---

### **Phase 5: Build & Test (1 giờ)**

#### Step 5.1: Clean Build

```bash
cd /path/to/xiaozhi-esp32

# Verify ADF path
echo $ADF_PATH

# Full clean build
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

#### Step 5.2: Flash & Monitor

```bash
idf.py flash monitor
```

#### Step 5.3: Test Cases

**Test 1: Play MP3 Radio**
```json
MCP Tool Call:
{
  "tool": "self.radio.play",
  "parameters": {
    "station_id": "test_mp3"
  }
}

Expected: BBC World Service plays
```

**Test 2: Play AAC Station (VOV1)**
```json
{
  "tool": "self.radio.play",
  "parameters": {
    "station_id": "vov1"
  }
}

Expected: VOV1 plays (with HLS parsing)
```

**Test 3: Volume Control**
```json
{
  "tool": "self.radio.set_volume",
  "parameters": {
    "volume": 50
  }
}

Expected: Volume changes to 50%
```

**Test 4: Get Status**
```json
{
  "tool": "self.radio.get_status"
}

Expected: Returns current station info
```

---

### **Phase 6: Audio Arbitration (1 giờ - Optional)**

Để tránh conflict giữa TTS và Radio:

**Tạo file: `main/audio/audio_manager.h`**

```cpp
#pragma once
#include <mutex>

class AudioManager {
public:
    enum Source {
        NONE,
        TTS,      // Priority 1
        RADIO     // Priority 2
    };
    
    static AudioManager& GetInstance();
    
    void SetActive(Source source);
    Source GetActive() const;
    bool CanPlayRadio() const;
    
private:
    AudioManager() : active_(NONE) {}
    Source active_;
    mutable std::mutex mutex_;
};
```

**Implementation:**

```cpp
#include "audio_manager.h"

AudioManager& AudioManager::GetInstance() {
    static AudioManager instance;
    return instance;
}

void AudioManager::SetActive(Source source) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = source;
}

AudioManager::Source AudioManager::GetActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

bool AudioManager::CanPlayRadio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ == NONE || active_ == RADIO;
}
```

**Sử dụng:**

Trong `AdfRadioPlayer::Play()`:
```cpp
auto& mgr = AudioManager::GetInstance();
if (!mgr.CanPlayRadio()) {
    ESP_LOGW(TAG, "TTS is active");
    return false;
}
mgr.SetActive(AudioManager::RADIO);
```

Trong `AudioService` khi TTS start:
```cpp
AudioManager::GetInstance().SetActive(AudioManager::TTS);
```

---

## 🎯 Expected Results

### ✅ Success Criteria

1. **MP3 Streaming**: BBC radio plays smoothly
2. **AAC Streaming**: VOV1 plays with AAC codec
3. **HLS Parsing**: m3u8 playlists auto-parsed
4. **Volume Control**: Changes apply immediately
5. **No Crashes**: Stable for 30+ minutes playback
6. **Memory Usage**: < 150KB RAM total

### 📊 Monitoring

```bash
# While playing, monitor:
idf.py monitor

# Look for:
I (xxx) ADF_Radio: Pipeline created successfully
I (xxx) ADF_Radio: Playback started
I (xxx) ADF_Radio: [ * ] Element running
```

---

## ⚠️ Common Issues & Solutions

### Issue 1: "ADF_PATH not found"
**Solution:**
```bash
export ADF_PATH=~/esp/esp-adf
echo $ADF_PATH  # Verify
```

### Issue 2: "audio_pipeline.h: No such file"
**Solution:** Check `EXTRA_COMPONENT_DIRS` trong root CMakeLists.txt

### Issue 3: I2S conflict
**Symptom:** Garbled audio or silence
**Solution:** 
- Ensure AudioCodec I2S initialized first
- Check I2S port number (should be 0)

### Issue 4: HLS timeout
**Symptom:** VOV stations don't play
**Solution:** 
- Check network connection
- Verify `enable_playlist_parser = true`

### Issue 5: Memory issues
**Solution:** Enable PSRAM in menuconfig:
```
Component config → ESP32S3-Specific → Support for external SPIRAM
```

---

## 📝 Summary

| Task | File | Action |
|------|------|--------|
| Setup | `CMakeLists.txt` (root) | Add ADF_PATH |
| Dependencies | `main/CMakeLists.txt` | Add ADF components |
| Radio Player | `main/audio/adf_radio_player.h` | Create |
| Radio Player | `main/audio/adf_radio_player.cc` | Create |
| MCP Tools | `main/mcp_server.cc` | Add radio tools |
| Init | `main/application.cc` | Initialize radio |

**Total Time:** ~4-6 hours
**Difficulty:** ⭐⭐⭐☆☆ (Medium)

---

## 🚀 Next Steps

1. ✅ Verify environment (Done)
2. **Start Phase 1**: Modify CMakeLists.txt
3. **Phase 2-4**: Create radio player + MCP integration
4. **Phase 5**: Build & Test
5. **Phase 6**: (Optional) Audio arbitration

**Ready to start Phase 1?** 🎵
