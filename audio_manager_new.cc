// audio_manager.cc - Audio arbitration implementation
// Create as: main/audio/audio_manager.cc

#include "audio_manager.h"

#define TAG "AudioManager"

AudioManager& AudioManager::GetInstance() {
    static AudioManager instance;
    return instance;
}

void AudioManager::SetActiveSource(AudioSource source) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (active_source_ == source) {
        return;
    }
    
    const char* source_names[] = {"NONE", "TTS", "RADIO"};
    ESP_LOGI(TAG, "Switching audio source: %s -> %s", 
             source_names[active_source_], 
             source_names[source]);
    
    active_source_ = source;
}

AudioManager::AudioSource AudioManager::GetActiveSource() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_source_;
}

bool AudioManager::CanPlayRadio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Radio can play only if no audio is active OR radio is already playing
    return active_source_ == SOURCE_NONE || active_source_ == SOURCE_RADIO;
}
