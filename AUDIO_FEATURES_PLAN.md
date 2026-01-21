# Kế hoạch Mở rộng tính năng Audio cho XiaoZhi

**Mục tiêu**: Bổ sung các tính năng nghe nhạc và audio streaming cho thiết bị XiaoZhi (ESP32-S3).

**Ngày tạo**: 2026-01-19

---

## 1. Tổng quan

### 1.1 Tính năng cần thêm
1. **Nghe Radio Internet** (Radio streaming)
2. **Phát file MP3** (Local + Network)
3. **Nghe nhạc từ YouTube** (YouTube Audio)
4. **Quản lý Playlist**

### 1.2 Kiến trúc hiện tại (Tóm tắt)

Dựa trên phân tích codebase, XiaoZhi hiện có:

*   **Audio Service** (`main/audio/audio_service.cc`): Service chính quản lý audio pipeline.
*   **Audio Codec** (`main/audio/codecs/`): Hỗ trợ nhiều codec (ES8311, ES8388, ES8389, ES8374...) qua I2S.
*   **Audio Processor**: AFE (Audio Front-End) cho wake word detection và noise cancellation.
*   **Protocol Layer**: Websocket/MQTT để truyền audio dạng OPUS tới/từ server.
*   **I2S Interface**: Giao tiếp phần cứng cho mic và speaker.

**Kiến trúc hiện tại chủ yếu phục vụ voice assistant**:  
`Microphone (I2S) → Audio Processor → OPUS Encoder → Websocket → Server`  
`Server → Websocket → OPUS Decoder → Audio Codec (I2S) → Speaker`

---

## 2. Thiết kế kiến trúc mới

### 2.1 Mô hình tổng quan

Để thêm tính năng streaming, cần xây dựng một **Audio Player Component** song song với voice assistant pipeline.

```
┌─────────────────────────────────────────────────────────┐
│              XiaoZhi Audio System (Refined)             │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌────────────────────────────────────────────────┐    │
│  │       Audio State Manager (NEW - Core)         │    │
│  │  States: IDLE, LISTENING, PLAYING, PAUSED      │    │
│  │  Priority: Wake Word > TTS > Music/Radio       │    │
│  └────────────┬───────────────────────────────────┘    │
│               │                                         │
│       ┌───────┴────────┐                                │
│       ▼                ▼                                │
│  ┌─────────┐    ┌──────────────┐                       │
│  │  Voice  │    │ Audio Player │                       │
│  │ Pipeline│    │  (ESP-ADF)   │                       │
│  │(Hiện tại)│    │              │                       │
│  └────┬────┘    └──────┬───────┘                       │
│       │                │                                │
│       └────────┬───────┘                                │
│                ▼                                        │
│       ┌────────────────┐                                │
│       │  Audio Codec   │                                │
│       │   (I2S Out)    │                                │
│       └────────────────┘                                │
│                                                         │
│  Note: NO Audio Mixer - Use State-based switching      │
└─────────────────────────────────────────────────────────┘
```

### 2.2 Component mới cần tạo

#### A. **Audio State Manager** (`main/audio/audio_state_manager.h/cc`) - **BẮT BUỘC**

**Vai trò**: Xương sống của hệ thống, quản lý state và priority.

**States**:
```cpp
enum class AudioState {
    IDLE,
    LISTENING,          // Wake word detected, processing voice
    PLAYING_MUSIC,
    PLAYING_RADIO,
    BUFFERING,
    PAUSED_BY_WAKEWORD, // Music paused, waiting for voice command
    ERROR
};
```

**Priority Rules**:
1. Wake Word > TTS Response > Music/Radio
2. Khi wake word trigger:
   - Pause/Stop current audio pipeline
   - Flush buffer
   - Transfer I2S control to voice pipeline
3. Sau khi voice xử lý xong → Resume music

**API**:
```cpp
class AudioStateManager {
    void RequestState(AudioState new_state, Priority priority);
    void OnWakeWordDetected();  // Auto pause music
    void OnVoiceCommandDone();  // Auto resume if needed
};
```

---

#### B. **Audio Player** (`main/audio/audio_player.h/cc`)

Wrapper around ESP-ADF pipeline.

**Trách nhiệm:**
*   Build ESP-ADF pipeline (http_stream → decoder → i2s_stream)
*   Quản lý playback state
*   Giao tiếp với State Manager

**KHÔNG tự viết**: HttpStream, RingBuffer (dùng ESP-ADF có sẵn)

**Design**:
```cpp
// Enum-based source type (không dùng OOP nặng)
enum class SourceType { RADIO, MP3_URL, LOCAL_FILE, YOUTUBE };

class AudioPlayer {
    bool Play(SourceType type, const std::string& url);
    void Stop();
    void Pause();
 private:
    audio_pipeline_handle_t pipeline_;  // ESP-ADF
};
```

#### B. **Audio Decoders** (`main/audio/decoders/`)

Thư viện decode các định dạng audio.

**ESP-IDF đã hỗ trợ sẵn** (qua ESP-ADF):
*   MP3 Decoder (libhelix-mp3)
*   AAC Decoder
*   WAV Decoder
*   FLAC Decoder

**Cần:**
*   Wrapper để tích hợp vào hệ thống.
*   Sử dụng ESP-ADF audio pipeline nếu có thể.

#### C. **ESP-ADF Integration** - **SỬ DỤNG CÓ SẴN**

**KHÔNG tự viết** HttpStream và RingBuffer!

ESP-ADF đã có đầy đủ:
- `http_stream` component
- `audio_pipeline` với ringbuffer tích hợp
- `i2s_stream` cho output
- `mp3_decoder`, `aac_decoder`

**Công việc**: Wrap và cấu hình ESP-ADF elements.

**Example Pipeline**:
```cpp
// ESP-ADF pipeline (Radio streaming)
audio_pipeline_cfg_t cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
pipeline = audio_pipeline_init(&cfg);

http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
http_cfg.enable_playlist_parser = true;
http_stream_reader = http_stream_init(&http_cfg);

mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
mp3_decoder = mp3_decoder_init(&mp3_cfg);

i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
i2s_stream_writer = i2s_stream_init(&i2s_cfg);

audio_pipeline_register(pipeline, http_stream_reader, "http");
audio_pipeline_register(pipeline, mp3_decoder, "mp3");
audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
audio_pipeline_link(pipeline, (const char*[]){"http", "mp3", "i2s"}, 3);
```

#### D. **YouTube Audio Adapter** (`main/integrations/youtube/`)

Component đặc biệt cho YouTube (phức tạp hơn).

**Vấn đề**: ESP32 không thể xử lý trực tiếp YouTube do:
*   Cần parse HTML/JavaScript (quá nặng).
*   YouTube API yêu cầu authentication.
*   Stream URL thay đổi thường xuyên.

**Giải pháp**:  
→ **Cần một Backend Server** (Python/Node.js) làm trung gian:
1.  ESP32 gửi yêu cầu "Play YouTube: [URL]" tới server.
2.  Server dùng `yt-dlp` hoặc YouTube API để lấy direct audio stream URL.
3.  Server trả về HTTP stream URL cho ESP32.
4.  ESP32 stream như bình thường.

**Hoặc**:  
→ Server có thể proxy luôn audio stream (tốn bandwidth server).

---

## 3. Chi tiết triển khai

### 3.1 Nghe Radio Internet

**Nguồn phổ biến**:
*   Shoutcast/Icecast streams (HTTP + ICY metadata).
*   TuneIn API (cần API key).
*   Danh sách URL radio tĩnh.

**Triển khai**:

1.  **Tạo Radio Source** (`radio_source.h/cc`):
    ```cpp
    class RadioSource : public AudioSource {
    public:
        void SetURL(const std::string& url);
        bool Connect() override;
        size_t Read(uint8_t* buffer, size_t size) override;
        void Disconnect() override;
    private:
        HttpStream http_stream_;
        std::string station_url_;
    };
    ```

2.  **HTTP Client** (Dùng ESP-IDF `esp_http_client`):
    *   Kết nối tới URL.
    *   Stream data về buffer.
    *   Parse ICY metadata để lấy tên bài hát (hiển thị trên màn hình).

3.  **Decoder**:
    *   Hầu hết radio stream là **MP3** hoặc **AAC**.
    *   Sử dụng ESP-ADF MP3 decoder.

4.  **Output**:
    *   Decode xong → ghi vào I2S DMA buffer → speaker.

**Voice Command**:
```
"Mở đài BBC Radio"
"Chơi radio VOV1"
"Tắt radio"
```

### 3.2 Phát file MP3

**Hai nguồn**:

#### A. **MP3 từ SD Card** (Local Storage)

*   Cần thêm SD card reader (SPI).
*   Quét thư mục `/music/` để tạo playlist.
*   Voice: "Phát nhạc số 1", "Phát tất cả nhạc".

**Triển khai**:
1.  Mount SD card (ESP-IDF FATFS).
2.  Tạo `LocalFileSource` class.
3.  Đọc file MP3 → MP3 decoder → I2S output.

#### B. **MP3 từ Network** (HTTP URL)

*   Tương tự Radio, nhưng file có độ dài xác định.
*   Hỗ trợ seeking (tua bài) nếu server hỗ trợ HTTP Range.

**Voice Command**:
```
"Phát file nhạc thứ 3"
"Phát bài XYZ.mp3"
"Dừng nhạc"
"Tiếp theo" (next track)
```

### 3.3 Nghe Audio từ YouTube

**Phương án đề xuất**:

#### Backend Server (Bắt buộc)

**Tech Stack**:
*   **Python + Flask/FastAPI** hoặc **Node.js + Express**.
*   Thư viện: `yt-dlp` (Python) hoặc `ytdl-core` (Node.js).

**API Endpoint**:
```
POST /api/youtube/stream
Body: { "video_id": "dQw4w9WgXcQ" }
Response: { "stream_url": "https://..." }
```

**Workflow**:
1.  User nói: "Phát YouTube: Never Gonna Give You Up".
2.  XiaoZhi gửi transcript tới XiaoZhi server (websocket hiện tại).
3.  Server nhận diện intent "play_youtube" + video ID hoặc search query.
4.  Server gọi `/api/youtube/stream` → backend trả URL.
5.  XiaoZhi nhận URL → stream như Radio.

**Backend Code mẫu (Python + yt-dlp) - REFINED**:
```python
from flask import Flask, request, jsonify
import yt_dlp

app = Flask(__name__)

@app.route('/api/youtube/stream', methods=['POST'])
def get_youtube_stream():
    video_id = request.json.get('video_id')
    url = f"https://www.youtube.com/watch?v={video_id}"
    
    ydl_opts = {
        # CRITICAL: Force format compatible with ESP32
        'format': 'bestaudio[ext=m4a]/bestaudio[acodec=aac]/bestaudio',
        'quiet': True,
        'no_warnings': True,
    }
    
    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(url, download=False)
        
        # Find best AAC/M4A stream (ESP32 có hardware AAC decoder)
        stream_url = None
        for fmt in info.get('formats', []):
            if fmt.get('acodec') in ['aac', 'mp4a'] and fmt.get('vcodec') == 'none':
                stream_url = fmt['url']
                break
        
        if not stream_url:
            stream_url = info['url']  # Fallback
        
    return jsonify({
        'stream_url': stream_url,
        'format': 'aac',  # Inform ESP32
        'expires_in': 21600  # 6 hours
    })
```

**Tại sao AAC/M4A**:
- ESP32-S3 có hardware AAC decoder
- Bandwidth thấp hơn MP3
- YouTube ưu tiên format này

**ESP32 Side**:
*   Nhận URL từ server.
*   Dùng `HttpStream` để tải về và decode (thường là OPUS hoặc M4A).

### 3.4 Quản lý Playlist

**Tính năng**:
*   Lưu danh sách bài hát/radio vào NVS (Non-Volatile Storage).
*   Voice: "Lưu radio này", "Phát playlist yêu thích".

**Triển khai**:
```cpp
class Playlist {
public:
    void Add(const std::string& title, const std::string& url);
    void Remove(size_t index);
    const PlaylistItem& Get(size_t index);
    void SaveToNVS();
    void LoadFromNVS();
private:
    std::vector<PlaylistItem> items_;
};
```

---

## 4.3 MCP (Model Context Protocol) Integration

XiaoZhi đã tích hợp sẵn **MCP Server** ngay trong firmware (`main/mcp_server.cc`).

### Cách hoạt động

```
User: "Mở đài VOV1"
    ↓
Voice → AI Server (Claude/GPT)
    ↓
AI gọi MCP Tool: "self.audio.play_radio"
    ↓
ESP32 nhận MCP message qua WebSocket
    ↓
mcp_server.cc parse & execute
    ↓
AudioPlayer bắt đầu phát radio
```

### Định nghĩa Tools mới

**Location**: `main/boards/felixnguyen-1.83-1mic/felixnguyen-1.83-1mic.cc` (hoặc board tương ứng)

**Example**:
```cpp
void FelixNguyenBoard::InitializeTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // Radio Tool
    mcp_server.AddTool("self.audio.play_radio",
        "Play internet radio station. Available stations: VOV1, VOV2, VOH, etc.",
        PropertyList({
            Property("station", kPropertyTypeString, "Station ID (vov1, voh, etc.)")
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto station = properties["station"].value<std::string>();
            RadioPlayer::GetInstance().Play(station);
            return true;
        });
    
    // Stop playback
    mcp_server.AddTool("self.audio.stop",
        "Stop current audio playback",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            AudioPlayer::GetInstance().Stop();
            return true;
        });
}
```

### Lợi ích

- ✅ **Voice control tự động**: AI hiểu câu nói → gọi tool phù hợp
- ✅ **Không cần hardcode commands**: Flexible hơn keyword matching
- ✅ **Dễ thêm features**: Chỉ cần implement tool mới
- ✅ **Standardized**: Follow MCP spec (JSON-RPC 2.0)

---

## 4. Dependency & Thư viện

### 4.1 ESP-IDF Components cần thêm

Thêm vào `idf_component.yml` hoặc `CMakeLists.txt`:

```yaml
dependencies:
  espressif/esp-adf: "^4.0"  # Audio Development Framework
  espressif/esp-adf-libs: "^1.0"  # MP3, AAC decoders
```

Hoặc sử dụng `idf.py add-dependency`:
```bash
idf.py add-dependency espressif/esp-adf
```

### 4.2 Thư viện cần thiết

*   **MP3 Decoder**: ESP-ADF có sẵn (libhelix).
*   **AAC Decoder**: ESP-ADF (libfdk-aac).
*   **HTTP Client**: ESP-IDF `esp_http_client`.
*   **JSON Parser**: ESP-IDF `cJSON` (đã có).

---

## 5. Kế hoạch triển khai (Phân giai đoạn)

### **Phase 1: Nền tảng Audio Player** (2-3 tuần)

**Mục tiêu**: Tạo audio player cơ bản, phát được file MP3 từ HTTP URL.

**Tasks**:
- [ ] Tạo component `audio_player` (class AudioPlayer).
- [ ] Tích hợp ESP-ADF MP3 decoder.
- [ ] Implement `HttpStream` class để tải audio từ URL.
- [ ] Test phát 1 URL MP3 cứng (hardcode) → speaker.
- [ ] Thêm lệnh voice debug: "Test phát nhạc".

**Output**: Có thể phát 1 file MP3 qua HTTP.

---

### **Phase 2: Radio Streaming** (1-2 tuần)

**Mục tiêu**: Phát radio từ danh sách URL tĩnh.

**Tasks**:
- [ ] Tạo `RadioSource` class.
- [ ] Thêm danh sách 5-10 đài radio phổ biến (hardcode).
- [ ] Parse ICY metadata để hiển thị tên bài hát.
- [ ] Tích hợp voice command: "Mở đài [tên]".
- [ ] Hiển thị thông tin radio trên màn hình.

**Output**: Có thể nghe radio qua voice command.

---

### **Phase 3: YouTube Audio Backend** (2 tuần)

**Mục tiêu**: Tạo backend server để lấy stream URL từ YouTube.

**Tasks (Backend)**:
- [ ] Tạo project Python/Node.js.
- [ ] Implement API `/api/youtube/stream`.
- [ ] Test với `yt-dlp` để lấy audio URL.
- [ ] Deploy server (local hoặc cloud).
- [ ] Bảo mật API (API key hoặc authentication).

**Tasks (ESP32)**:
- [ ] Tạo `YoutubeSource` class.
- [ ] Gọi API backend để lấy stream URL.
- [ ] Stream audio từ URL nhận được.
- [ ] Voice command: "Phát YouTube: [tên bài]".

**Output**: Có thể nghe audio YouTube qua voice.

---

### **Phase 4: Local MP3 Playback** (1 tuần)

**Mục tiêu**: Phát file MP3 từ SD card.

**Tasks**:
- [ ] Thêm SD card reader (hardware).
- [ ] Mount SD card trong firmware.
- [ ] Tạo `LocalFileSource` class.
- [ ] Quét thư mục `/music/` để tạo playlist.
- [ ] Voice: "Phát nhạc số [n]".

**Output**: Có thể phát MP3 local.

---

### **Phase 5: Quản lý Playlist & UI** (1-2 tuần)

**Mục tiêu**: Lưu playlist, điều khiển playback (pause, next, prev).

**Tasks**:
- [ ] Implement Playlist manager (save/load từ NVS).
- [ ] Thêm UI hiển thị playlist trên màn hình.
- [ ] Voice: "Lưu bài này", "Tiếp theo", "Quay lại".
- [ ] Buttons control (nếu có phím cứng).

**Output**: Hệ thống hoàn chỉnh.

---

## 6. Thách thức & Giải pháp

### 6.1 Memory (RAM/Flash) & PSRAM Strategy

**Vấn đề**: ESP32-S3 có internal RAM hạn chế (~500KB usable).

**Chiến lược PSRAM** (board có PSRAM 8MB):

**Phân bổ**:
- ✅ **Audio buffer** → PSRAM (http_stream ringbuffer, decoder buffer)
- ✅ **LVGL framebuffer** → PSRAM
- ❌ **Decoder stack** → Internal RAM (cần tốc độ)
- ❌ **Voice pipeline** → Internal RAM (real-time critical)

**Heap Management**:
```cpp
// Cấu hình ESP-ADF để dùng PSRAM
#define CONFIG_AUDIO_BOARD_CUSTOM
#define CONFIG_ESP32_SPIRAM_SUPPORT

// Trong audio_pipeline config
cfg.rb_size = 8 * 1024;  // Ringbuffer 8KB
cfg.task_stack = 4096;   
cfg.out_rb_size = 12 * 1024; // PSRAM
```

**Tránh fragmentation**:
- Hạn chế `malloc/free` động
- Ưu tiên static allocation cho critical paths
- Dùng memory pool cho audio chunks
- ESP-ADF pipeline tự quản lý buffer lifecycle

### 6.2 Concurrency (Xung đột với Voice Assistant)

**Vấn đề**: Không thể vừa nghe nhạc vừa voice assistant.

**Giải pháp DUY NHẤT (v1)**: **Audio Focus / State Machine**

**KHÔNG dùng Audio Mixer** vì:
- ❌ Mixing CPU-intensive trên ESP32-S3
- ❌ Gây miss wake word (AFE sensitivity giảm)
- ❌ Audio glitches, khó debug
- ❌ RAM overhead

**Audio Focus Pattern**:
```cpp
void AudioStateManager::OnWakeWordDetected() {
    if (current_state_ == AudioState::PLAYING_MUSIC ||
        current_state_ == AudioState::PLAYING_RADIO) {
        
        // 1. Pause music pipeline
        audio_pipeline_pause(music_pipeline_);
        
        // 2. Flush I2S buffer (tránh echo)
        i2s_driver_flush();
        
        // 3. Switch to voice mode
        current_state_ = AudioState::PAUSED_BY_WAKEWORD;
        voice_pipeline_active_ = true;
    }
}

void AudioStateManager::OnVoiceCommandDone() {
    if (current_state_ == AudioState::PAUSED_BY_WAKEWORD) {
        // Resume music after 500ms (cho TTS phát xong)
        vTaskDelay(pdMS_TO_TICKS(500));
        audio_pipeline_resume(music_pipeline_);
        current_state_ = resume_to_state_; // PLAYING_MUSIC/RADIO
    }
}
```

**Ưu điểm**:
- ✅ Đơn giản, ổn định
- ✅ Không miss wake word
- ✅ UX tốt (pause rõ ràng)

### 6.3 Network Stability

**Vấn đề**: WiFi mất kết nối → audio bị gián đoạn.

**Giải pháp**:
*   Buffering (buffer ít nhất 5-10s audio).
*   Auto-reconnect logic.
*   Hiển thị trạng thái buffering trên màn hình.

### 6.4 YouTube Copyright & Rate Limiting

**Vấn đề**: Backend có thể bị YouTube block nếu request quá nhiều.

**Giải pháp**:
*   Cache stream URL (có thời hạn từ YouTube).
*   Rotate IP nếu cần (dùng proxy).
*   Hạn chế số lượng request/user.

---

## 7. Tài nguyên tham khảo

### Thư viện & Docs
*   [ESP-ADF Documentation](https://docs.espressif.com/projects/esp-adf/en/latest/)
*   [ESP-IDF HTTP Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/esp_http_client.html)
*   [yt-dlp GitHub](https://github.com/yt-dlp/yt-dlp)
*   [ICY Metadata Format](http://www.smackfu.com/stuff/programming/shoutcast.html)

### Example Projects
*   [ESP32 Internet Radio Example](https://github.com/espressif/esp-adf/tree/master/examples/player/pipeline_http_mp3)
*   [ESP-ADF MP3 Player](https://github.com/espressif/esp-adf/tree/master/examples/get-started/play_mp3)

---

## 8. Checklist tổng hợp

### Chuẩn bị
- [ ] Nghiên cứu ESP-ADF documentation.
- [ ] Setup môi trường build với ESP-ADF.
- [ ] Chuẩn bị backend server (nếu làm YouTube).

### Development
- [ ] Implement Audio Player core.
- [ ] Test MP3 streaming từ HTTP.
- [ ] Implement Radio streaming.
- [ ] Implement YouTube integration (với backend).
- [ ] Implement Local MP3 playback (nếu có SD card).
- [ ] Implement Playlist management.

### Testing
- [ ] Test với nhiều định dạng audio (MP3, AAC).
- [ ] Test tính ổn định khi mất kết nối.
- [ ] Test memory usage (không overflow).
- [ ] Test voice commands.

### Documentation
- [ ] Viết README cho từng component.
- [ ] Ghi lại API của Audio Player.
- [ ] Hướng dẫn setup backend (nếu cần).

---

## 9. Kết luận

Dự án có tính khả thi cao. ESP32-S3 đủ mạnh để xử lý audio streaming.  
**Điểm chính**:
*   Sử dụng ESP-ADF để giảm công sức implement decoder.
*   Backend server bắt buộc cho YouTube.
*   Radio streaming đơn giản nhất (bắt đầu từ đây).
*   Cần cân nhắc user experience (auto-pause khi có lệnh voice).

**Thời gian ước tính**: 6-8 tuần (part-time).
