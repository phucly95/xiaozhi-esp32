#ifndef FONT_AWESOME_H
#define FONT_AWESOME_H

#ifdef __cplusplus
extern "C" {
#endif

// Battery icons
#define FONT_AWESOME_BATTERY_FULL           "\xEF\x89\x80" // 
#define FONT_AWESOME_BATTERY_THREE_QUARTERS "\xEF\x89\x81" // 
#define FONT_AWESOME_BATTERY_HALF           "\xEF\x89\x82" // 
#define FONT_AWESOME_BATTERY_QUARTER        "\xEF\x89\x83" // 
#define FONT_AWESOME_BATTERY_EMPTY          "\xEF\x89\x84" // 
#define FONT_AWESOME_BATTERY_BOLT           "\xEF\x83\xA7" // 

// Volume/Audio
#define FONT_AWESOME_VOLUME_XMARK           "\xEF\x9A\xA9" // 

// Network/Signal
#define FONT_AWESOME_SIGNAL_OFF             "\xEF\x80\x92" //  (Empty)
#define FONT_AWESOME_SIGNAL_WEAK            "\xEF\x9A\xA0" //  (1 bar)
#define FONT_AWESOME_SIGNAL_FAIR            "\xEF\x9A\xA2" //  (2 bars)
#define FONT_AWESOME_SIGNAL_GOOD            "\xEF\x9A\xA4" //  (3 bars)
#define FONT_AWESOME_SIGNAL_STRONG          "\xEF\x80\x92" //  (4 bars) - Note: Adjust if specific strong icon needed, often same as full

// WiFi
#define FONT_AWESOME_WIFI                   "\xEF\x87\xAB" // 
#define FONT_AWESOME_WIFI_SLASH             "\xEF\x9A\xAC" //  (WiFi Slash/Off) - provisional
#define FONT_AWESOME_WIFI_WEAK              "\xEF\x9A\xA9" //  (Using generic connection/weak icon provisionally)
#define FONT_AWESOME_WIFI_FAIR              "\xEF\x9A\xAA" // 

// AI/System
#define FONT_AWESOME_MICROCHIP_AI           "\xEF\xA0\xB0" //  (Approx)
#define FONT_AWESOME_NEUTRAL                "\xEF\x84\x9A" //  (Meh/Neutral)

/**
 * @brief Get the UTF-8 string for a named FontAwesome icon (e.g. emotion)
 * 
 * @param name The name of the icon/emotion
 * @return const char* The UTF-8 string or NULL if not found
 */
const char* font_awesome_get_utf8(const char* name);

#ifdef __cplusplus
}
#endif

#endif // FONT_AWESOME_H
