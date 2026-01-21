#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>

/**
 * Radio Station Configuration
 * Scalable design to easily add new stations
 */

struct RadioStation {
    std::string id;          // Unique ID (e.g., "vov1", "voh")
    std::string name;        // Display name (e.g., "VOV1 - Đài Tiếng nói Việt Nam")
    std::string url;         // Stream URL (HTTP/HTTPS)
    std::string genre;       // Genre (e.g., "news", "music", "talk")
    int bitrate;            // Bitrate (kbps)
    std::string format;      // Audio format (mp3, aac, etc.)
    
    RadioStation(const std::string& id, const std::string& name, 
                 const std::string& url, const std::string& genre = "general",
                 int bitrate = 128, const std::string& format = "mp3")
        : id(id), name(name), url(url), genre(genre), bitrate(bitrate), format(format) {}
};

/**
 * Radio Stations Database
 * 
 * Design Pattern: Singleton Registry
 * Easy to extend: Just add new entries to stations_ map
 */
class RadioStationsDB {
public:
    static RadioStationsDB& GetInstance() {
        static RadioStationsDB instance;
        return instance;
    }

    // Get station by ID
    const RadioStation* GetStation(const std::string& id) const {
        auto it = stations_.find(id);
        if (it != stations_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Get all stations (for UI listing)
    std::vector<RadioStation> GetAllStations() const {
        std::vector<RadioStation> result;
        for (const auto& pair : stations_) {
            result.push_back(pair.second);
        }
        return result;
    }

    // Get stations by genre
    std::vector<RadioStation> GetStationsByGenre(const std::string& genre) const {
        std::vector<RadioStation> result;
        for (const auto& pair : stations_) {
            if (pair.second.genre == genre) {
                result.push_back(pair.second);
            }
        }
        return result;
    }

    // Get station names (for MCP tool description)
    std::string GetStationListDescription() const {
        std::string desc = "Available stations: ";
        bool first = true;
        for (const auto& pair : stations_) {
            if (!first) desc += ", ";
            desc += pair.second.id + " (" + pair.second.name + ")";
            first = false;
        }
        return desc;
    }

private:
    RadioStationsDB() {
        InitializeStations();
    }

    void InitializeStations() {
        // Vietnamese National Radio Stations
        // VOV (Voice of Vietnam) - Updated January 2025
        AddStation("vov1", "VOV1 - Đài Tiếng nói Việt Nam",
                   "http://media.kythuatvov.vn:1936/live/VOV1.sdp/playlist.m3u8",
                   "news", 48, "aac");
        
        AddStation("vov2", "VOV2 - Kênh Đời sống",
                   "http://media.kythuatvov.vn:1936/live/VOV2.sdp/playlist.m3u8",
                   "lifestyle", 48, "aac");
        
        AddStation("vov3", "VOV3 - Kênh Âm nhạc",
                   "http://media.kythuatvov.vn:1936/live/VOV3.sdp/playlist.m3u8",
                   "music", 48, "aac");
        
        AddStation("vov5", "VOV5 - Kênh Dân tộc",
                   "http://media.kythuatvov.vn:1936/live/VOV5.sdp/playlist.m3u8",
                   "ethnic", 48, "aac");
        
        // VOH (Voice of Ho Chi Minh City)
        AddStation("voh", "VOH 99.9MHz - Đài TPHCM",
                   "http://mediatech.vncdn.vn/voh/voh.m3u8",
                   "news", 128, "mp3");
        
        AddStation("voh_fm95", "VOH FM 95.6MHz - Giao thông",
                   "http://mediatech.vncdn.vn/voh/vohfm95.6.m3u8",
                   "traffic", 128, "mp3");
        
        // HNR (Hanoi Radio)
        AddStation("hnr", "VOV Giao thông Hà Nội",
                   "http://media.cntv.vn:1935/live/vov_giao_thong_hn/playlist.m3u8",
                   "traffic", 96, "mp3");
        
        // Popular Music Stations
        AddStation("nrg", "NRG Radio - Nhạc trẻ",
                   "http://stream.nrg.com.vn:1935/radio/nrgstream_source/playlist.m3u8",
                   "music", 128, "aac");
        
        // International (Examples - for testing)
        AddStation("bbc", "BBC World Service",
                   "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service",
                   "news", 128, "mp3");
                   
        // Tested MP3 Direct Stream (Use this for first test!)
        AddStation("test_mp3", "Test Radio (BBC MP3)",
                   "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service", 
                   "test", 128, "mp3");
        
        // Add more stations here...
        // Easy to extend: just add new AddStation() calls
    }

    void AddStation(const std::string& id, const std::string& name,
                    const std::string& url, const std::string& genre = "general",
                    int bitrate = 128, const std::string& format = "mp3") {
        stations_.emplace(id, RadioStation(id, name, url, genre, bitrate, format));
    }

    std::map<std::string, RadioStation> stations_;
};
