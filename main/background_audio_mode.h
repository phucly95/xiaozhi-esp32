#ifndef BACKGROUND_AUDIO_MODE_H
#define BACKGROUND_AUDIO_MODE_H

/**
 * BackgroundAudioMode - Defines the type of background audio playback
 * 
 * This allows the system to properly manage audio focus and state transitions
 * when background audio (radio, music, podcasts) is playing while user
 * interacts with the voice assistant.
 * 
 * Design principles:
 * - Background audio should pause when user is interacting (Listening/Speaking)
 * - Background audio should resume when system returns to Idle
 * - Multiple background audio sources are exclusive (only one at a time)
 */
enum BackgroundAudioMode {
    kBackgroundAudioNone = 0,           // No background audio
    kBackgroundAudioRadio,              // Radio streaming
    kBackgroundAudioYouTubeStream,      // YouTube audio stream
    kBackgroundAudioMusic,              // Local music playback
    kBackgroundAudioPodcast,            // Podcast streaming
    // Future extensions can be added here
};

#endif // BACKGROUND_AUDIO_MODE_H
