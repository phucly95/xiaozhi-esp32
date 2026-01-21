// radio_player.cc - CORRECT Implementation with AAC + HLS Parser
// Replace: main/audio/radio_player.cc

#include "radio_player.h"
#include <string.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "esp_crt_bundle.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "minimp3.h"

// AAC Decoder (CORRECT API)
#include "esp_audio_dec.h"
#include "esp_aac_dec.h"

#define TAG "RadioPlayer"

// Buffer sizes
#define COMPRESSED_BUFFER_SIZE (64 * 1024)
#define PCM_BUFFER_SIZE (32 * 1024)
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
    
    // Create ring buffers in PSRAM
    compressed_buffer_ = new RingBuffer(COMPRESSED_BUFFER_SIZE, true);
    pcm_buffer_ = new RingBuffer(PCM_BUFFER_SIZE, true);
    
    if (!compressed_buffer_ || !pcm_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        return false;
    }
    
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
    std::string stream_url;
    char* line = strtok(buffer, "\n\r");
    
    while (line != nullptr) {
        // Skip comments and empty lines
        if (line[0] != '#' && strlen(line) > 0) {
            // Found a stream URL
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
            
            ESP_LOGI(TAG, "Found HLS stream: %s", stream_url.c_str());
            break;
        }
        line = strtok(nullptr, "\n\r");
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
        // MP3 decoder (minimp3 - already initialized)
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
    const char* ct = esp_http_client_get_header(http_client_, "Content-Type");
    if (ct) {
        content_type = ct;
        ESP_LOGI(TAG, "Content-Type: %s", ct);
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
    xTaskCreatePinnedToCore(HttpStreamTask, "http_stream", 8192, this, 5, &http_task_, 0);
    xTaskCreatePinnedToCore(DecoderTask, "decoder", 8192, this, 4, &decoder_task_, 0);
    xTaskCreatePinnedToCore(I2SOutputTask, "i2s_output", 4096, this, 3, &i2s_task_, 1);
    
    state_ = State::PLAYING;
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
}

void RadioPlayer::SetVolume(int level) {
    volume_ = level < 0 ? 0 : (level > 100 ? 100 : level);
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
    
    while (!should_stop_) {
        int read_len = esp_http_client_read(http_client_, (char*)buffer, 4096);
        
        if (read_len > 0) {
            size_t written = compressed_buffer_->Write(buffer, read_len, pdMS_TO_TICKS(1000));
            if (written < read_len) {
                ESP_LOGW(TAG, "Buffer full, wrote %d/%d", written, read_len);
            }
        } else if (read_len == 0) {
            ESP_LOGW(TAG, "Stream ended");
            break;
        } else {
            ESP_LOGE(TAG, "Read error: %d", read_len);
            break;
        }
    }
    
    free(buffer);
    ESP_LOGI(TAG, "HTTP task ended");
}

// Decoder Task - Multi-format with CORRECT API
void RadioPlayer::DecoderTask(void* param) {
    RadioPlayer* player = static_cast<RadioPlayer*>(param);
    player->DecoderLoop();
    vTaskDelete(nullptr);
}

void RadioPlayer::DecoderLoop() {
    ESP_LOGI(TAG, "Decoder task started (codec: %d)", (int)codec_type_);
    
    uint8_t* input_buf = (uint8_t*)malloc(4096);
    int16_t* pcm_buf = (int16_t*)heap_caps_malloc(
        8192 * sizeof(int16_t), 
        MALLOC_CAP_SPIRAM
    );
    
    if (codec_type_ == CodecType::MP3) {
        // MP3 decoding
        mp3d_sample_t* pcm_float = (mp3d_sample_t*)heap_caps_malloc(
            MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t), 
            MALLOC_CAP_SPIRAM
        );
        
        while (!should_stop_) {
            size_t read = compressed_buffer_->Read(input_buf, 4096, pdMS_TO_TICKS(100));
            if (read == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(
                (mp3dec_t*)mp3_decoder_, 
                input_buf, 
                read, 
                pcm_float, 
                &info
            );
            
            if (samples > 0) {
                // Convert float to int16 with volume
                for (int i = 0; i < samples * info.channels; i++) {
                    float sample = pcm_float[i] * (volume_ / 100.0f);
                    if (sample > 32767.0f) sample = 32767.0f;
                    if (sample < -32768.0f) sample = -32768.0f;
                    pcm_buf[i] = (int16_t)sample;
                }
                
                size_t pcm_bytes = samples * info.channels * sizeof(int16_t);
                pcm_buffer_->Write((uint8_t*)pcm_buf, pcm_bytes, pdMS_TO_TICKS(1000));
            }
        }
        
        free(pcm_float);
    }
    else if (codec_type_ == CodecType::AAC) {
        // AAC decoding with CORRECT API
        esp_audio_dec_in_raw_t raw = {};
        esp_audio_dec_out_frame_t frame = {};
        
        raw.buffer = input_buf;
        frame.buffer = (uint8_t*)pcm_buf;
        frame.len = 8192 * sizeof(int16_t);
        
        while (!should_stop_) {
            size_t read = compressed_buffer_->Read(input_buf, 4096, pdMS_TO_TICKS(100));
            if (read == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            raw.len = read;
            raw.consumed = 0;
            
            // Process all consumed data
            while (raw.len > 0 && !should_stop_) {
                esp_audio_err_t ret = esp_audio_dec_process(aac_decoder_, &raw, &frame);
                
                if (ret == ESP_AUDIO_ERR_OK && frame.decoded_size > 0) {
                    // Apply volume
                    int16_t* samples = (int16_t*)frame.buffer;
                    int num_samples = frame.decoded_size / sizeof(int16_t);
                    
                    for (int i = 0; i < num_samples; i++) {
                        int32_t sample = samples[i] * volume_ / 100;
                        if (sample > 32767) sample = 32767;
                        if (sample < -32768) sample = -32768;
                        samples[i] = (int16_t)sample;
                    }
                    
                    pcm_buffer_->Write(frame.buffer, frame.decoded_size, pdMS_TO_TICKS(1000));
                    
                    // Update for next iteration
                    raw.buffer += raw.consumed;
                    raw.len -= raw.consumed;
                } else if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    ESP_LOGW(TAG, "Buffer too small, need %d bytes", frame.needed_size);
                    break;
                } else {
                    // Consume partial data and continue
                    if (raw.consumed > 0) {
                        raw.buffer += raw.consumed;
                        raw.len -= raw.consumed;
                    } else {
                        break; // No progress, skip this chunk
                    }
                }
            }
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
    
    int16_t* buffer = (int16_t*)heap_caps_malloc(2048 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    std::vector<int16_t> vec_buffer;
    vec_buffer.reserve(2048);
    
    while (!should_stop_) {
        size_t bytes_read = pcm_buffer_->Read((uint8_t*)buffer, 2048 * sizeof(int16_t), pdMS_TO_TICKS(100));
        
        if (bytes_read > 0) {
            size_t samples_read = bytes_read / sizeof(int16_t);
            vec_buffer.assign(buffer, buffer + samples_read);
            
            if (codec_) {
                codec_->OutputData(vec_buffer);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
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
