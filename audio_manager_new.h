// audio_manager.h - Audio arbitration between TTS and Radio
// Create as: main/audio/audio_manager.h

#pragma once

#include <mutex>
#include <esp_log.h>

/**
 * Audio Manager
 * 
 * Purpose: Manage audio output priority between TTS and Radio
 * 
 * Priority:
 * 1. TTS (AudioService) - Highest priority
 * 2. Radio (RadioPlayer) - Lower priority
 * 
 * When TTS starts playing, Radio will be paused automatically.
 * When TTS finishes, Radio can resume if needed.
 */

class AudioManager {
public:
    enum AudioSource {
        SOURCE_NONE,     // No active audio
        SOURCE_TTS,      // AudioService (priority 1)
        SOURCE_RADIO     // RadioPlayer (priority 2)
    };

    static AudioManager& GetInstance() {
        static AudioManager instance;
        return instance;
    }
    
    // Set the current active audio source
    void SetActiveSource(AudioSource source);
    
    // Get current active source
    AudioSource GetActiveSource() const;
    
    // Check if Radio can play (TTS is not active)
    bool CanPlayRadio() const;
    
    // Check if TTS can play (always true - highest priority)
    bool CanPlayTTS() const { return true; }

private:
    AudioManager() : active_source_(SOURCE_NONE) {}
    ~AudioManager() = default;
    
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;
    
    AudioSource active_source_;
    mutable std::mutex mutex_;
};
