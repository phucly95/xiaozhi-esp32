// radio_player.cc - CORRECT Implementation with AAC + HLS Parser
// Replace: main/audio/radio_player.cc

#include "radio_player.h"

#include "application.h" // Needed for AudioService access
#include <string.h>
#include <cmath>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "esp_crt_bundle.h"

#define MINIMP3_IMPLEMENTATION
// Use INT16 output (not float) for simplicity and less memory
// #define MINIMP3_FLOAT_OUTPUT  // DISABLED - using int16 output
#include "minimp3.h"

// AAC Decoder (CORRECT API)
#include "esp_audio_dec.h"
#include "esp_aac_dec.h"

#define TAG "RadioPlayer"

// Buffer sizes - Using PSRAM so can be larger
#define COMPRESSED_BUFFER_SIZE (64 * 1024)  // 64KB compressed data
#define PCM_BUFFER_SIZE (64 * 1024)          // 64KB PCM = ~2 seconds at 16kHz mono
#define HLS_BUFFER_SIZE (4096)

RadioPlayer::RadioPlayer()
    : state_(State::IDLE)
    , current_station_(nullptr)
    , codec_type_(CodecType::UNKNOWN)
    , http_client_(nullptr)
    , compressed_buffer_(nullptr)
    , pcm_buffer_(nullptr)
    , mp3_decoder_(nullptr)
    , aac_decoder_(nullptr)
    , codec_(nullptr)
    , http_task_(nullptr)
    , decoder_task_(nullptr)
    , i2s_task_(nullptr)
    , should_stop_(false)
    , volume_(70)
{
    // MP3 decoder init
    mp3_decoder_ = malloc(sizeof(mp3dec_t));
    if (mp3_decoder_) {
        mp3dec_init((mp3dec_t*)mp3_decoder_);
    }
}

RadioPlayer::~RadioPlayer() {
    Stop();
    CleanupDecoder();
    if (compressed_buffer_) delete compressed_buffer_;
    if (pcm_buffer_) delete pcm_buffer_;
}

bool RadioPlayer::Initialize(AudioCodec* codec) {
    ESP_LOGI(TAG, "Initializing Radio Player with Multi-Codec + HLS support");
    
    codec_ = codec;
    
    // Register AAC decoder only (we use our own minimp3 for MP3)
    // This avoids symbol conflict with esp_audio_codec's MP3 decoder
    esp_audio_err_t reg_ret = esp_aac_dec_register();
    if (reg_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Failed to register AAC decoder: %d (may already be registered)", reg_ret);
    } else {
        ESP_LOGI(TAG, "Registered AAC decoder");
    }
    
    // Create ring buffers in PSRAM
    compressed_buffer_ = new RingBuffer(COMPRESSED_BUFFER_SIZE, true);
    pcm_buffer_ = new RingBuffer(PCM_BUFFER_SIZE, true);
    
    if (!compressed_buffer_ || !pcm_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        return false;
    }
    
    // Verify buffers are working
    ESP_LOGI(TAG, "Compressed buffer free space: %d", (int)compressed_buffer_->FreeSpace());
    ESP_LOGI(TAG, "PCM buffer free space: %d", (int)pcm_buffer_->FreeSpace());
    
    ESP_LOGI(TAG, "Radio Player initialized");
    return true;
}

// ========== MAGIC HLS PARSER ==========
// Auto parse .m3u8 playlist to get real stream URL
std::string RadioPlayer::ParseHLSPlaylist(const std::string& m3u8_url) {
    ESP_LOGI(TAG, "Parsing HLS playlist: %s", m3u8_url.c_str());
    
    // Create temporary HTTP client for HLS parsing
    esp_http_client_config_t config = {};
    config.url = m3u8_url.c_str();
    config.timeout_ms = 5000;
    config.buffer_size = HLS_BUFFER_SIZE;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HLS parser client");
        return "";
    }
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HLS open failed");
        esp_http_client_cleanup(client);
        return "";
    }
    
    esp_http_client_fetch_headers(client);
    
    // Read m3u8 content
    char* buffer = (char*)malloc(HLS_BUFFER_SIZE);
    int total_read = 0;
    int read_len;
    
    while ((read_len = esp_http_client_read(client, buffer + total_read, 
                                            HLS_BUFFER_SIZE - total_read - 1)) > 0) {
        total_read += read_len;
        if (total_read >= HLS_BUFFER_SIZE - 1) break;
    }
    
    buffer[total_read] = '\0';
    
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    // Parse m3u8 to find stream URL
    // For live streams, we want the LAST segment (newest), not the first
    std::string stream_url;
    std::string last_segment_url;
    char* line = strtok(buffer, "\n\r");
    
    while (line != nullptr) {
        // Skip comments and empty lines
        if (line[0] != '#' && strlen(line) > 0) {
            // Found a stream URL - save it (we want the last one for live streams)
            stream_url = line;
            
            // If relative URL, make it absolute
            if (stream_url.find("http") != 0) {
                // Extract base URL
                size_t last_slash = m3u8_url.find_last_of('/');
                if (last_slash != std::string::npos) {
                    std::string base_url = m3u8_url.substr(0, last_slash + 1);
                    stream_url = base_url + stream_url;
                }
            }
            
            // Keep updating to get the LAST one (newest segment for live streams)
            last_segment_url = stream_url;
        }
        line = strtok(nullptr, "\n\r");
    }
    
    // Use the last segment found
    if (!last_segment_url.empty()) {
        ESP_LOGI(TAG, "Found HLS stream: %s", last_segment_url.c_str());
        stream_url = last_segment_url;
    }
    
    free(buffer);
    
    if (stream_url.empty()) {
        ESP_LOGE(TAG, "No valid stream found in HLS playlist");
        return "";
    }
    
    // Check if the found URL is another m3u8 (nested playlist)
    if (stream_url.find(".m3u8") != std::string::npos) {
        ESP_LOGI(TAG, "Nested playlist detected, parsing again...");
        return ParseHLSPlaylist(stream_url); // Recursive parse
    }
    
    return stream_url;
}

// Auto-detect codec from format and content-type
RadioPlayer::CodecType RadioPlayer::DetectCodecType(const std::string& format, 
                                                     const std::string& content_type) {
    // Check format string first
    if (format == "mp3") {
        return CodecType::MP3;
    } else if (format == "aac" || format == "m4a") {
        return CodecType::AAC;
    }
    
    // Check content-type header
    if (content_type.find("mp3") != std::string::npos || 
        content_type.find("mpeg") != std::string::npos) {
        return CodecType::MP3;
    } else if (content_type.find("aac") != std::string::npos ||
               content_type.find("mp4") != std::string::npos ||
               content_type.find("m4a") != std::string::npos) {
        return CodecType::AAC;
    }
    
    // Default to MP3 if unknown
    ESP_LOGW(TAG, "Unknown codec type, defaulting to MP3");
    return CodecType::MP3;
}

bool RadioPlayer::InitializeDecoder(CodecType type) {
    codec_type_ = type;
    
    if (type == CodecType::MP3) {
        // MP3 decoder - reallocate if it was freed by CleanupDecoder
        if (!mp3_decoder_) {
            mp3_decoder_ = malloc(sizeof(mp3dec_t));
            if (!mp3_decoder_) {
                ESP_LOGE(TAG, "Failed to allocate MP3 decoder!");
                return false;
            }
        }
        mp3dec_init((mp3dec_t*)mp3_decoder_);
        ESP_LOGI(TAG, "Using minimp3 decoder");
        return true;
    } 
    else if (type == CodecType::AAC) {
        // AAC decoder (CORRECT API)
        esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        
        esp_audio_dec_cfg_t dec_cfg = {
            .type = ESP_AUDIO_TYPE_AAC,
            .cfg = &aac_cfg,
            .cfg_sz = sizeof(aac_cfg),
        };
        
        esp_audio_err_t ret = esp_audio_dec_open(&dec_cfg, &aac_decoder_);
        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Failed to init AAC decoder: %d", ret);
            return false;
        }
        
        ESP_LOGI(TAG, "Using ESP AAC decoder");
        return true;
    }
    
    ESP_LOGE(TAG, "Unsupported codec type");
    return false;
}

void RadioPlayer::CleanupDecoder() {
    if (aac_decoder_) {
        esp_audio_dec_close(aac_decoder_);
        aac_decoder_ = nullptr;
    }
    
    if (mp3_decoder_) {
        free(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
}

bool RadioPlayer::Play(const std::string& station_id) {
    auto& db = RadioStationsDB::GetInstance();
    auto station = db.GetStation(station_id);
    
    if (!station) {
        ESP_LOGE(TAG, "Station not found: %s", station_id.c_str());
        return false;
    }
    
    ESP_LOGI(TAG, "Playing: %s (%s)", station->name.c_str(), station->format.c_str());
    
    Stop();
    
    current_station_ = station;
    state_ = State::CONNECTING;
    
    // === MAGIC HLS PARSER ===
    // Check if URL is m3u8 playlist
    std::string stream_url = station->url;
    if (stream_url.find(".m3u8") != std::string::npos) {
        ESP_LOGI(TAG, "HLS playlist detected, parsing...");
        stream_url = ParseHLSPlaylist(stream_url);
        
        if (stream_url.empty()) {
            ESP_LOGE(TAG, "Failed to parse HLS playlist");
            state_ = State::ERROR;
            return false;
        }
        
        ESP_LOGI(TAG, "Using stream URL: %s", stream_url.c_str());
    }
    
    // Clear buffers
    compressed_buffer_->Clear();
    pcm_buffer_->Clear();
    
    // Create HTTP client
    esp_http_client_config_t config = {};
    config.url = stream_url.c_str();
    config.event_handler = HttpEventHandler;
    config.user_data = this;
    config.timeout_ms = 5000;
    config.buffer_size = 4096;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    
    http_client_ = esp_http_client_init(&config);
    if (!http_client_) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        state_ = State::ERROR;
        return false;
    }
    
    // Set ICY metadata header
    esp_http_client_set_header(http_client_, "Icy-MetaData", "1");
    
    // Open connection to detect codec type
    esp_err_t err = esp_http_client_open(http_client_, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        state_ = State::ERROR;
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
        return false;
    }
    
    esp_http_client_fetch_headers(http_client_);
    
    // Get content-type for codec detection
    std::string content_type;
    char* ct_val = nullptr;
    esp_err_t hdr_err = esp_http_client_get_header(http_client_, "Content-Type", &ct_val);
    if (hdr_err == ESP_OK && ct_val) {
        content_type = ct_val;
        ESP_LOGI(TAG, "Content-Type: %s", ct_val);
    }
    
    // Auto-detect codec
    CodecType codec_type = DetectCodecType(station->format, content_type);
    if (!InitializeDecoder(codec_type)) {
        state_ = State::ERROR;
        esp_http_client_close(http_client_);
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
        return false;
    }
    
    // Create tasks
    should_stop_ = false;
    
    // IMPORTANT: Enable audio output for radio playback
    // The StateMachine may have disabled it when leaving speaking state
    if (codec_) {
        codec_->EnableOutput(true);
        ESP_LOGI(TAG, "Enabled audio output for radio");
    }
    
    // Start tasks with proper priorities:
    // HTTP (5) - Downloads data on Core 0
    // Decoder (4) - Decodes on Core 0 (same as HTTP for data locality)
    // I2S Output (6) - HIGHEST priority on Core 1 for real-time output
    // NOTE: Keep stack sizes small to conserve memory (total ~32KB)
    BaseType_t ret;
    
    ret = xTaskCreatePinnedToCore(HttpStreamTask, "http_stream", 4096, this, 5, &http_task_, 0); // 4KB
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create HTTP task!");
    }
    
    ret = xTaskCreatePinnedToCore(DecoderTask, "decoder", 12288, this, 4, &decoder_task_, 1); // 12KB on Core 1
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Decoder task! ret=%d", ret);
    }
    
    ret = xTaskCreatePinnedToCore(I2SOutputTask, "i2s_output", 6144, this, 6, &i2s_task_, 1); // 6KB
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create I2S task!");
        // If I2S fails, we should stop the other tasks
        should_stop_ = true;
        state_ = State::ERROR;
        return false;
    }
    
    state_ = State::PLAYING;
    
    // === PRODUCTION AUDIO MODE ===
    // 1. Set background audio mode so system knows radio is active
    Application::GetInstance().SetBackgroundAudioMode(kBackgroundAudioRadio);
    
    // 2. Force transition to Idle (wake-word mode) immediately
    //    This is called from main task so happens after TTS queue is processed
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    ESP_LOGI(TAG, "Forced DeviceState to Idle for wake-word detection");
    
    return true;
}

void RadioPlayer::Stop() {
    if (state_ == State::IDLE) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping");
    
    should_stop_ = true;
    state_ = State::IDLE;
    
    // Wait for tasks to finish
    vTaskDelay(pdMS_TO_TICKS(500));
    
    http_task_ = nullptr;
    decoder_task_ = nullptr;
    i2s_task_ = nullptr;
    
    // Cleanup
    if (http_client_) {
        esp_http_client_close(http_client_);
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
    }
    
    CleanupDecoder();
    
    if (compressed_buffer_) compressed_buffer_->Clear();
    if (pcm_buffer_) pcm_buffer_->Clear();
    
    current_station_ = nullptr;
    
    // === PRODUCTION AUDIO MODE ===
    // Clear background audio mode when radio stops
    Application::GetInstance().SetBackgroundAudioMode(kBackgroundAudioNone);
}

void RadioPlayer::SetVolume(int level) {
    volume_ = level < 0 ? 0 : (level > 100 ? 100 : level);
}

// Test I2S hardware with 1KHz sine wave
void RadioPlayer::TestTone(int duration_ms) {
    ESP_LOGI(TAG, "=== TESTING I2S OUTPUT WITH 1KHz TONE ===");
    
    if (!codec_) {
        ESP_LOGE(TAG, "TestTone: No codec available!");
        return;
    }
    
    // Enable output
    codec_->EnableOutput(true);
    
    // Generate 1KHz sine wave at 16000 Hz sample rate
    const int sample_rate = 16000;
    const int frequency = 1000;
    const int amplitude = 10000;  // ~30% volume
    const int samples_per_cycle = sample_rate / frequency;
    
    std::vector<int16_t> tone_buffer;
    tone_buffer.reserve(1024);
    
    int total_samples = (sample_rate * duration_ms) / 1000;
    int phase = 0;
    
    ESP_LOGI(TAG, "Playing %d ms of 1KHz tone (%d samples)", duration_ms, total_samples);
    
    while (total_samples > 0) {
        tone_buffer.clear();
        
        int chunk = (total_samples > 512) ? 512 : total_samples;
        for (int i = 0; i < chunk; i++) {
            // Simple sine approximation using lookup or math
            float angle = (2.0f * 3.14159f * phase) / samples_per_cycle;
            int16_t sample = (int16_t)(amplitude * sinf(angle));
            tone_buffer.push_back(sample);  // Mono or duplicate for stereo
            phase = (phase + 1) % samples_per_cycle;
        }
        
        codec_->OutputData(tone_buffer);
        total_samples -= chunk;
    }
    
    ESP_LOGI(TAG, "=== TONE TEST COMPLETE ===");
}

// HTTP Stream Task
void RadioPlayer::HttpStreamTask(void* param) {
    RadioPlayer* player = static_cast<RadioPlayer*>(param);
    player->HttpStreamLoop();
    vTaskDelete(nullptr);
}

void RadioPlayer::HttpStreamLoop() {
    ESP_LOGI(TAG, "HTTP task started (connection already open)");
    
    int status = esp_http_client_get_status_code(http_client_);
    if (status != 200) {
        ESP_LOGE(TAG, "Invalid HTTP status: %d", status);
        state_ = State::ERROR;
        return;
    }
    
    uint8_t* buffer = (uint8_t*)malloc(4096);
    int total_read = 0;
    int segment_count = 0;
    
    // Check if this is an HLS stream (we stored the chunklist URL during Play())
    bool is_hls = (current_station_ && 
                   current_station_->url.find(".m3u8") != std::string::npos);
    
    while (!should_stop_) {
        int read_len = esp_http_client_read(http_client_, (char*)buffer, 4096);
        
        if (read_len > 0) {
            total_read += read_len;
            size_t written = compressed_buffer_->Write(buffer, read_len, pdMS_TO_TICKS(1000));
            
            // Debug: Log write result on first few writes
            static int write_log_count = 0;
            if (write_log_count++ < 5) {
                ESP_LOGI(TAG, "HTTP Write: %d bytes read, %d bytes written to buffer", read_len, (int)written);
            }
            
            if (written < (size_t)read_len) {
                // Buffer full - log less frequently
                static int full_log_count = 0;
                if (full_log_count++ % 100 == 0) {
                    ESP_LOGW(TAG, "HTTP: Buffer full, wrote %d/%d (msg throttled)", (int)written, read_len);
                }
                vTaskDelay(pdMS_TO_TICKS(50)); // Slow down download slightly
            }
            // Log every 50KB
            if (total_read % 51200 < 4096) {
                ESP_LOGI(TAG, "HTTP: Downloaded %dKB", total_read / 1024);
            }
        } else if (read_len == 0) {
            // Segment ended
            segment_count++;
            
            if (is_hls && !should_stop_) {
                // HLS: Close current segment and fetch next from chunklist
                ESP_LOGI(TAG, "HLS: Segment %d complete (%dKB), fetching next...", 
                         segment_count, total_read / 1024);
                
                // Store the current segment URL to avoid replaying it
                char url_buffer[256];
                esp_http_client_get_url(http_client_, url_buffer, sizeof(url_buffer));
                std::string last_segment_url = url_buffer;
                
                esp_http_client_close(http_client_);
                esp_http_client_cleanup(http_client_);
                
                // Wait a bit for new segment to become available
                // HLS segments are typically 6-10 seconds, wait ~2 seconds
                vTaskDelay(pdMS_TO_TICKS(2000));
                
                // Re-parse chunklist to get next segment
                std::string next_segment;
                int retry_count = 0;
                const int MAX_RETRIES = 5;
                
                // Extract sequence number from last segment URL (e.g., "media_w123_551214.aac" -> "551214")
                auto extract_sequence = [](const std::string& url) -> std::string {
                    size_t last_underscore = url.rfind('_');
                    size_t dot_pos = url.rfind('.');
                    if (last_underscore != std::string::npos && dot_pos != std::string::npos && dot_pos > last_underscore) {
                        return url.substr(last_underscore + 1, dot_pos - last_underscore - 1);
                    }
                    return "";
                };
                
                std::string last_sequence = extract_sequence(last_segment_url);
                ESP_LOGI(TAG, "HLS: Last segment sequence: %s", last_sequence.c_str());
                
                // Keep trying until we get a NEW segment (different sequence number)
                while (retry_count < MAX_RETRIES && !should_stop_) {
                    next_segment = ParseHLSPlaylist(current_station_->url);
                    
                    if (next_segment.empty()) {
                        ESP_LOGE(TAG, "HLS: Failed to get next segment");
                        break;
                    }
                    
                    // Compare sequence numbers
                    std::string new_sequence = extract_sequence(next_segment);
                    if (new_sequence != last_sequence && !new_sequence.empty()) {
                        // Different sequence number - this is a new segment
                        ESP_LOGI(TAG, "HLS: New segment sequence: %s", new_sequence.c_str());
                        break;
                    }
                    
                    // Same sequence - wait and retry
                    ESP_LOGW(TAG, "HLS: Got same sequence %s, waiting for new one (retry %d)...", 
                             new_sequence.c_str(), retry_count + 1);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    retry_count++;
                }
                
                std::string final_sequence = extract_sequence(next_segment);
                if (next_segment.empty() || final_sequence == last_sequence) {
                    ESP_LOGE(TAG, "HLS: Could not get new segment after %d retries", MAX_RETRIES);
                    break;
                }
                
                // Open new segment
                esp_http_client_config_t config = {};
                config.url = next_segment.c_str();
                config.timeout_ms = 5000;
                config.buffer_size = 4096;
                
                http_client_ = esp_http_client_init(&config);
                if (!http_client_) {
                    ESP_LOGE(TAG, "HLS: Failed to init HTTP for next segment");
                    break;
                }
                
                if (esp_http_client_open(http_client_, 0) != ESP_OK) {
                    ESP_LOGE(TAG, "HLS: Failed to open next segment");
                    break;
                }
                
                esp_http_client_fetch_headers(http_client_);
                total_read = 0; // Reset for new segment
                ESP_LOGI(TAG, "HLS: Started segment %d", segment_count + 1);
                
            } else {
                // Non-HLS: Stream truly ended
                ESP_LOGW(TAG, "Stream ended");
                break;
            }
        } else {
            ESP_LOGE(TAG, "Read error: %d", read_len);
            break;
        }
    }
    
    free(buffer);
    ESP_LOGI(TAG, "HTTP task ended (downloaded %d segments, %dKB total)", segment_count, total_read / 1024);
}

// Decoder Task - Multi-format with CORRECT API
void RadioPlayer::DecoderTask(void* param) {
    ESP_LOGI(TAG, "DecoderTask ENTRY, param=%p", param);
    if (!param) {
        ESP_LOGE(TAG, "DecoderTask: null param!");
        vTaskDelete(nullptr);
        return;
    }
    RadioPlayer* player = static_cast<RadioPlayer*>(param);
    ESP_LOGI(TAG, "DecoderTask: calling DecoderLoop");
    player->DecoderLoop();
    ESP_LOGI(TAG, "DecoderTask: DecoderLoop returned");
    vTaskDelete(nullptr);
}

void RadioPlayer::DecoderLoop() {
    ESP_LOGI(TAG, "Decoder task started (codec: %d)", (int)codec_type_);
    ESP_LOGI(TAG, "Decoder: compressed_buffer_=%p, pcm_buffer_=%p", compressed_buffer_, pcm_buffer_);
    
    uint8_t* input_buf = (uint8_t*)malloc(4096);
    int16_t* pcm_buf = (int16_t*)heap_caps_malloc(
        8192 * sizeof(int16_t), 
        MALLOC_CAP_SPIRAM
    );
    
    if (codec_type_ == CodecType::MP3) {
        // MP3 decoding with ACCUMULATOR BUFFER
        // We need to keep unprocessed bytes between reads
        const size_t ACCUM_SIZE = 8192;
        uint8_t* accum_buf = (uint8_t*)malloc(ACCUM_SIZE);
        if (!accum_buf) {
            ESP_LOGE(TAG, "Failed to allocate accumulator buffer");
            state_ = State::ERROR;
            return;
        }
        
        size_t bytes_in_accum = 0;
        
        while (!should_stop_) {
            // 1. Read from ring buffer into accumulator
            // Leave some margin or read in chunks
            size_t space_left = ACCUM_SIZE - bytes_in_accum;
            
            // Don't read if full (shouldn't happen if we consume correctly)
            if (space_left > 0) {
                // Read up to 1024 bytes at a time or space_left
                size_t to_read = (space_left > 2048) ? 2048 : space_left;
                
                size_t bytes_read = compressed_buffer_->Read(accum_buf + bytes_in_accum, to_read, pdMS_TO_TICKS(10));
                
                if (bytes_read > 0) {
                    bytes_in_accum += bytes_read;
                    
                    // periodic log
                    static int read_cnt = 0;
                    if (read_cnt++ % 200 == 0) {
                        ESP_LOGI(TAG, "Accumulator: %d bytes buffered", (int)bytes_in_accum);
                    }
                } else if (bytes_in_accum == 0) {
                    // Buffer empty and no new data
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
            }
            
            // 2. Decode as many frames as possible
            uint8_t* ptr = accum_buf;
            size_t remaining = bytes_in_accum;
            size_t consumed_total = 0;
            
            // Diagnostic: log first few iterations
            static int loop_iter = 0;
            if (loop_iter++ < 10) {
                ESP_LOGI(TAG, "Decode iter %d: %d bytes in accum", loop_iter, (int)bytes_in_accum);
            }
            
            while (remaining > 100) { // Need enough bytes for frame header
                mp3dec_frame_info_t info;
                int samples = mp3dec_decode_frame(
                    (mp3dec_t*)mp3_decoder_, 
                    ptr, 
                    remaining, 
                    pcm_buf, 
                    &info
                );
                
                // Ensure decode_cnt is declared for periodic logging
                static int decode_cnt = 0;
                
                // Log first 10 decodes unconditionally, then every 100
                if (decode_cnt < 10 || decode_cnt % 100 == 0) {
                    ESP_LOGI(TAG, "Decode #%d: samples=%d, hz=%d, ch=%d, bytes=%d", 
                        decode_cnt, samples, info.hz, info.channels, info.frame_bytes);
                }
                decode_cnt++;

                bool consumed = false;
                
                if (samples > 0) {
                    // Valid frame
                    consumed = true;
                    
                    // === AUDIO PROCESSING PIPELINE ===
                    // 1. Downmix Stereo -> Mono
                    // 2. Resample to 16000Hz (if needed)
                    // 3. Apply volume
                    
                    int out_samples = 0;
                    
                    // Target rate
                    const int TARGET_RATE = 16000;
                    
                    if (info.hz != TARGET_RATE || info.channels > 1) {
                         // Need processing
                         
                         // Step 1: Mono Conversion
                         int mono_samples = samples;
                         if (info.channels == 2) {
                             for (int i = 0; i < samples; i++) {
                                 int32_t mixed = ((int32_t)pcm_buf[2*i] + (int32_t)pcm_buf[2*i+1]) / 2;
                                 pcm_buf[i] = (int16_t)mixed;
                             }
                         }
                         
                         // Step 2: Resampling (OPTIMIZED)
                         // Use fixed-point math and static buffer for real-time performance
                         if (info.hz != TARGET_RATE) {
                             // Pre-calculate ratio in Q16 fixed-point for speed
                             // ratio = source_rate / target_rate (e.g., 24000/16000 = 1.5)
                             // Q16: multiply by 65536
                             uint32_t ratio_q16 = ((uint32_t)info.hz << 16) / TARGET_RATE;
                             int new_sample_count = (mono_samples * TARGET_RATE) / info.hz;
                             
                             // Static pre-allocated buffer (max 2048 samples output)
                             // MP3 frame max: 1152 samples @ 24kHz -> 768 samples @ 16kHz
                             static int16_t resample_buf[2048];
                             if (new_sample_count > 2048) new_sample_count = 2048;
                             
                             // Fixed-point linear interpolation (Q16)
                             for (int i = 0; i < new_sample_count; i++) {
                                 uint32_t src_idx_q16 = (uint32_t)i * ratio_q16;
                                 int idx0 = src_idx_q16 >> 16;
                                 int idx1 = idx0 + 1;
                                 uint32_t frac = (src_idx_q16 & 0xFFFF); // Q16 fractional part
                                 
                                 if (idx1 < mono_samples) {
                                     // Linear interpolation: out = s0*(1-frac) + s1*frac
                                     int32_t s0 = pcm_buf[idx0];
                                     int32_t s1 = pcm_buf[idx1];
                                     int32_t val = s0 + (((s1 - s0) * (int32_t)frac) >> 16);
                                     resample_buf[i] = (int16_t)val;
                                 } else if (idx0 < mono_samples) {
                                     resample_buf[i] = pcm_buf[idx0];
                                 } else {
                                     resample_buf[i] = 0;
                                 }
                             }
                             
                             // Apply volume
                             for (int i = 0; i < new_sample_count; i++) {
                                 int32_t val = resample_buf[i] * volume_ / 100;
                                 if (val > 32767) val = 32767;
                                 if (val < -32768) val = -32768;
                                 resample_buf[i] = (int16_t)val;
                             }
                             
                             pcm_buffer_->Write((uint8_t*)resample_buf, new_sample_count * sizeof(int16_t), pdMS_TO_TICKS(50));
                             out_samples = new_sample_count;
                         } else {
                             // Downmix only
                             for (int i = 0; i < mono_samples; i++) {
                                 int32_t val = pcm_buf[i] * volume_ / 100;
                                 if (val > 32767) val = 32767;
                                 if (val < -32768) val = -32768;
                                 pcm_buf[i] = (int16_t)val;
                             }
                             pcm_buffer_->Write((uint8_t*)pcm_buf, mono_samples * sizeof(int16_t), pdMS_TO_TICKS(50));
                             out_samples = mono_samples;
                         }
                    } 
                    else {
                        // Pass through (already 16k Mono)
                        int total_samples = samples * info.channels;
                        for (int i = 0; i < total_samples; i++) {
                            int32_t val = pcm_buf[i] * volume_ / 100;
                            if (val > 32767) val = 32767;
                            if (val < -32768) val = -32768;
                            pcm_buf[i] = (int16_t)val;
                        }
                        pcm_buffer_->Write((uint8_t*)pcm_buf, total_samples * sizeof(int16_t), pdMS_TO_TICKS(50));
                        out_samples = total_samples;
                    }
                    
                    if (out_samples > 0 && decode_cnt % 100 == 1) {
                        // ESP_LOGI(TAG, "Processed: %d samples", out_samples);
                    }
                    
                } else if (info.frame_bytes > 0) {
                    // Invalid frame / Skipped bytes (metadata or sync search)
                    // Just consume and move on
                    consumed = true;
                } else {
                    // Not enough data for a frame, or sync lost at end of buffer
                    break;
                }
                
                if (consumed) {
                    ptr += info.frame_bytes;
                    remaining -= info.frame_bytes;
                    consumed_total += info.frame_bytes;
                }
            }
            
            // 3. Move remaining data to start of buffer
            if (consumed_total > 0) {
                if (remaining > 0) {
                    memmove(accum_buf, ptr, remaining);
                }
                bytes_in_accum = remaining;
            } else if (bytes_in_accum >= ACCUM_SIZE && consumed_total == 0) {
                // STUCK: Buffer full but can't decode anything. 
                // Discard one byte to try to re-sync
                ESP_LOGW(TAG, "Accumulator stuck full, discarding 1 byte to resync");
                bytes_in_accum--;
                memmove(accum_buf, accum_buf + 1, bytes_in_accum);
            }
            
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        
        free(accum_buf);
    }
    else if (codec_type_ == CodecType::AAC) {
        // AAC decoding with 24kHz stereo assumption for VOV
        esp_audio_dec_in_raw_t raw = {};
        esp_audio_dec_out_frame_t frame = {};
        
        raw.buffer = input_buf;
        frame.buffer = (uint8_t*)pcm_buf;
        frame.len = 8192 * sizeof(int16_t);
        
        int decode_cnt = 0;
        int detected_sample_rate = 0;
        int detected_channels = 0;
        
        ESP_LOGI(TAG, "AAC decoder loop starting");
        
        // Static resample buffer
        static int16_t resample_buf[4096];
        
        while (!should_stop_) {
            size_t read = compressed_buffer_->Read(input_buf, 4096, pdMS_TO_TICKS(100));
            if (read == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            raw.buffer = input_buf;
            raw.len = read;
            raw.consumed = 0;
            
            while (raw.len > 0 && !should_stop_) {
                // Use generic process function (esp_audio_dec_open returns generic handle)
                esp_audio_err_t ret = esp_audio_dec_process(aac_decoder_, &raw, &frame);
                
                // Log on first successful decode
                if (ret == ESP_AUDIO_ERR_OK && frame.decoded_size > 0) {
                    if (detected_sample_rate == 0) {
                        // VOV AAC streams are typically 24kHz mono
                        // 2048 samples decoded per frame = 1024 stereo frames
                        detected_sample_rate = 24000;
                        detected_channels = 2;  // ADTS AAC is usually stereo
                        ESP_LOGI(TAG, "AAC Stream detected: assuming %dHz, %d channels",
                            detected_sample_rate, detected_channels);
                    }
                }
                
                if (decode_cnt < 10 || decode_cnt % 100 == 0) {
                    ESP_LOGI(TAG, "AAC Decode #%d: ret=%d, decoded=%d, consumed=%d",
                        decode_cnt, ret, (int)frame.decoded_size, (int)raw.consumed);
                }
                decode_cnt++;
                
                if (ret == ESP_AUDIO_ERR_OK && frame.decoded_size > 0) {
                    int16_t* samples = (int16_t*)frame.buffer;
                    int total_samples = frame.decoded_size / sizeof(int16_t);
                    int channels = detected_channels > 0 ? detected_channels : 2;
                    int sample_rate = detected_sample_rate > 0 ? detected_sample_rate : 24000;
                    
                    // Step 1: Stereo to Mono downmix (if stereo)
                    int mono_samples = total_samples;
                    if (channels == 2) {
                        int stereo_frames = total_samples / 2;
                        for (int i = 0; i < stereo_frames; i++) {
                            int32_t mixed = ((int32_t)samples[2*i] + (int32_t)samples[2*i+1]) / 2;
                            samples[i] = (int16_t)mixed;
                        }
                        mono_samples = stereo_frames;
                    }
                    
                    // Step 2: Resample to 16kHz
                    const int TARGET_RATE = 16000;
                    int out_samples = mono_samples;
                    int16_t* out_buffer = samples;
                    
                    if (sample_rate != TARGET_RATE) {
                        // Calculate resample ratio using fixed-point Q16
                        uint32_t ratio_q16 = ((uint32_t)sample_rate << 16) / TARGET_RATE;
                        out_samples = (mono_samples * TARGET_RATE) / sample_rate;
                        if (out_samples > 4096) out_samples = 4096;
                        
                        // Linear interpolation resampling
                        for (int i = 0; i < out_samples; i++) {
                            uint32_t src_idx_q16 = (uint32_t)i * ratio_q16;
                            int idx0 = src_idx_q16 >> 16;
                            int idx1 = idx0 + 1;
                            uint32_t frac = (src_idx_q16 & 0xFFFF);
                            
                            if (idx1 < mono_samples) {
                                int32_t s0 = samples[idx0];
                                int32_t s1 = samples[idx1];
                                int32_t val = s0 + (((s1 - s0) * (int32_t)frac) >> 16);
                                resample_buf[i] = (int16_t)val;
                            } else if (idx0 < mono_samples) {
                                resample_buf[i] = samples[idx0];
                            }
                        }
                        out_buffer = resample_buf;
                    }
                    
                    // Step 3: Apply volume
                    for (int i = 0; i < out_samples; i++) {
                        int32_t val = out_buffer[i] * volume_ / 100;
                        if (val > 32767) val = 32767;
                        if (val < -32768) val = -32768;
                        out_buffer[i] = (int16_t)val;
                    }
                    
                    pcm_buffer_->Write((uint8_t*)out_buffer, out_samples * sizeof(int16_t), pdMS_TO_TICKS(50));
                }
                
                if (raw.consumed > 0) {
                    raw.buffer += raw.consumed;
                    raw.len -= raw.consumed;
                    raw.consumed = 0;
                } else if (ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGW(TAG, "AAC decoder error %d, skipping %d bytes", ret, (int)raw.len);
                    break;
                } else {
                    break;
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    free(input_buf);
    free(pcm_buf);
    ESP_LOGI(TAG, "Decoder task ended");
}

// I2S Output Task
void RadioPlayer::I2SOutputTask(void* param) {
    RadioPlayer* player = static_cast<RadioPlayer*>(param);
    player->I2SOutputLoop();
    vTaskDelete(nullptr);
}

void RadioPlayer::I2SOutputLoop() {
    ESP_LOGI(TAG, "I2S task started");
    
    // Use larger buffer for smoother playback
    const int BUFFER_SAMPLES = 4096;
    int16_t* buffer = (int16_t*)heap_caps_malloc(BUFFER_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    std::vector<int16_t> vec_buffer;
    vec_buffer.reserve(BUFFER_SAMPLES);
    
    // === PRE-BUFFERING ===
    // Wait until we have at least 500ms of audio in buffer before starting playback
    // This prevents underrun at start and provides cushion for network jitter
    const size_t MIN_BUFFER_BYTES = 16000;  // 16KB = ~500ms at 16kHz mono 16-bit
    ESP_LOGI(TAG, "Pre-buffering (waiting for %d bytes)...", (int)MIN_BUFFER_BYTES);
    int prebuffer_timeout = 100;  // 5 seconds max wait
    while (!should_stop_ && pcm_buffer_->Available() < MIN_BUFFER_BYTES && prebuffer_timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        prebuffer_timeout--;
    }
    ESP_LOGI(TAG, "Pre-buffer complete: %d bytes available", (int)pcm_buffer_->Available());
    
    int loop_count = 0;
    
    while (!should_stop_) {
        loop_count++;
        
        // === PRODUCTION SMART DUCKING ===
        // Pause radio playback when user is interacting with Lily
        DeviceState device_state = Application::GetInstance().GetDeviceState();
        bool should_pause = (device_state == kDeviceStateListening ||
                             device_state == kDeviceStateSpeaking ||
                             device_state == kDeviceStateConnecting);
        
        if (should_pause) {
            vTaskDelay(pdMS_TO_TICKS(20)); // Shorter delay for faster response
            continue;
        }
        
        // Keep audio output enabled - but only every 100 loops to reduce overhead
        if (loop_count % 100 == 0) {
            Application::GetInstance().GetAudioService().KeepAlive();
        }

        // Read with short timeout to keep loop responsive
        size_t bytes_read = pcm_buffer_->Read((uint8_t*)buffer, BUFFER_SAMPLES * sizeof(int16_t), pdMS_TO_TICKS(20));
        
        if (bytes_read > 0) {
            size_t samples_read = bytes_read / sizeof(int16_t);
            
            // Digital Volume Boost (3x gain)
            for (size_t i = 0; i < samples_read; i++) {
                int32_t val = (int32_t)buffer[i] * 3;
                if (val > 32767) val = 32767;
                if (val < -32768) val = -32768;
                buffer[i] = (int16_t)val;
            }
            
            vec_buffer.assign(buffer, buffer + samples_read);
            
            if (codec_) {
                codec_->OutputData(vec_buffer);
            }
        } else {
            // No data available, yield briefly
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    free(buffer);
    ESP_LOGI(TAG, "I2S task ended");
}

esp_err_t RadioPlayer::HttpEventHandler(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP connected");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "Header: %s: %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "HTTP disconnected");
        break;
    default:
        break;
    }
    return ESP_OK;
}
