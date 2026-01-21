// radio_player.h - Enhanced version with CORRECT API + HLS Parser
// Replace: main/audio/radio_player.h

#pragma once

#include <string>
#include <esp_http_client.h>
#include <esp_log.h>
#include <driver/i2s_std.h>
#include "radio_stations.h"
#include "ring_buffer.h"
#include "audio_codec.h"

// ESP Audio Codec headers (CORRECT API)
#include "esp_audio_dec.h"

/**
 * Enhanced Radio Player
 * Features:
 * - Multi-format support: MP3 & AAC
 * - Auto codec detection from content-type
 * - HLS (.m3u8) playlist parser (Magic!)
 * - Architecture: HTTP Stream → Decoder → I2S Output
 */

class RadioPlayer {
public:
    enum class State {
        IDLE,
        CONNECTING,
        PLAYING,
        ERROR
    };
    
    enum class CodecType {
        UNKNOWN,
        MP3,
        AAC
    };

    static RadioPlayer& GetInstance() {
        static RadioPlayer instance;
        return instance;
    }

    bool Initialize(AudioCodec* codec);
    bool Play(const std::string& station_id);
    void Stop();
    void SetVolume(int level); // 0-100
    
    State GetState() const { return state_; }
    const RadioStation* GetCurrentStation() const { return current_station_; }

private:
    RadioPlayer();
    ~RadioPlayer();
    
    RadioPlayer(const RadioPlayer&) = delete;
    RadioPlayer& operator=(const RadioPlayer&) = delete;

    // === MAGIC HLS PARSER ===
    // Auto parse .m3u8 playlist to extract real stream URL
    std::string ParseHLSPlaylist(const std::string& m3u8_url);
    
    // Decoder selection & initialization
    CodecType DetectCodecType(const std::string& format, const std::string& content_type = "");
    bool InitializeDecoder(CodecType type);
    void CleanupDecoder();
    
    // Task functions
    static void HttpStreamTask(void* param);
    static void DecoderTask(void* param);
    static void I2SOutputTask(void* param);
    
    void HttpStreamLoop();
    void DecoderLoop();
    void I2SOutputLoop();
    
    // HTTP event handler
    static esp_err_t HttpEventHandler(esp_http_client_event_t* evt);
    
    State state_;
    const RadioStation* current_station_;
    CodecType codec_type_;
    
    // HTTP client
    esp_http_client_handle_t http_client_;
    
    // Ring buffers (PSRAM)
    RingBuffer* compressed_buffer_;  // Compressed audio data
    RingBuffer* pcm_buffer_;         // Decoded PCM
    
    // Multi-decoder support
    void* mp3_decoder_;                      // minimp3 decoder (void* = mp3dec_t*)
    esp_audio_dec_handle_t aac_decoder_;     // ESP AAC decoder (CORRECT TYPE)
    
    // Audio Codec
    AudioCodec* codec_;
    
    // Tasks
    TaskHandle_t http_task_;
    TaskHandle_t decoder_task_;
    TaskHandle_t i2s_task_;
    
    bool should_stop_;
    int volume_; // 0-100
};
