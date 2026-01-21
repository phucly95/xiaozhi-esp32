// mcp_tools_radio.cpp - MCP Tools for Radio Control
// Add this to: main/mcp_server.cc (inside AddCommonTools())

// ========== RADIO CONTROL MCP TOOLS ==========
// Add these tools to McpServer::AddCommonTools()

void McpServer::AddRadioTools() {
    auto& radio = RadioPlayer::GetInstance();
    
    // Tool 1: Play Radio Station
    AddTool("self.radio.play",
        "Play an internet radio station. " + 
        RadioStationsDB::GetInstance().GetStationListDescription() +
        "\n\nExample station IDs: vov1 (VOV1), vov2 (VOV2), vov3 (VOV3), test_mp3 (BBC)",
        PropertyList({
            Property("station_id", kPropertyTypeString)
        }),
        [&radio](const PropertyList& properties) -> ReturnValue {
            std::string station_id = properties["station_id"].value<std::string>();
            
            bool success = radio.Play(station_id);
            
            if (success) {
                auto station = radio.GetCurrentStation();
                if (station) {
                    return std::string("Playing: ") + station->name;
                }
                return std::string("Playing station: ") + station_id;
            }
            
            return std::string("Failed to play station: ") + station_id + 
                   ". Check station ID or network connection.";
        });
    
    // Tool 2: Stop Radio
    AddTool("self.radio.stop",
        "Stop the currently playing radio station.",
        PropertyList(),
        [&radio](const PropertyList& properties) -> ReturnValue {
            radio.Stop();
            return "Radio stopped";
        });
    
    // Tool 3: Set Radio Volume
    AddTool("self.radio.set_volume",
        "Set radio volume (0-100). Note: This affects the audio speaker volume globally.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }),
        [&radio](const PropertyList& properties) -> ReturnValue {
            int volume = properties["volume"].value<int>();
            radio.SetVolume(volume);
            return std::string("Radio volume set to ") + std::to_string(volume);
        });
    
    // Tool 4: Get Radio Status
    AddTool("self.radio.get_status",
        "Get current radio player status including state (idle/playing/connecting/error) and current station info.",
        PropertyList(),
        [&radio](const PropertyList& properties) -> ReturnValue {
            cJSON* json = cJSON_CreateObject();
            
            // Get state
            auto state = radio.GetState();
            const char* state_str = "idle";
            if (state == RadioPlayer::State::PLAYING) state_str = "playing";
            else if (state == RadioPlayer::State::CONNECTING) state_str = "connecting";
            else if (state == RadioPlayer::State::ERROR) state_str = "error";
            
            cJSON_AddStringToObject(json, "state", state_str);
            
            // Get current station info
            auto station = radio.GetCurrentStation();
            if (station) {
                cJSON* station_json = cJSON_CreateObject();
                cJSON_AddStringToObject(station_json, "id", station->id.c_str());
                cJSON_AddStringToObject(station_json, "name", station->name.c_str());
                cJSON_AddStringToObject(station_json, "genre", station->genre.c_str());
                cJSON_AddStringToObject(station_json, "format", station->format.c_str());
                cJSON_AddNumberToObject(station_json, "bitrate", station->bitrate);
                cJSON_AddItemToObject(json, "current_station", station_json);
            } else {
                cJSON_AddNullToObject(json, "current_station");
            }
            
            return json;
        });
    
    // Tool 5: List Available Stations
    AddTool("self.radio.list_stations",
        "List all available radio stations with their details.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& db = RadioStationsDB::GetInstance();
            auto stations = db.GetAllStations();
            
            cJSON* json = cJSON_CreateObject();
            cJSON* stations_array = cJSON_CreateArray();
            
            for (const auto& station : stations) {
                cJSON* station_json = cJSON_CreateObject();
                cJSON_AddStringToObject(station_json, "id", station.id.c_str());
                cJSON_AddStringToObject(station_json, "name", station.name.c_str());
                cJSON_AddStringToObject(station_json, "genre", station.genre.c_str());
                cJSON_AddStringToObject(station_json, "format", station.format.c_str());
                cJSON_AddNumberToObject(station_json, "bitrate", station.bitrate);
                cJSON_AddItemToArray(stations_array, station_json);
            }
            
            cJSON_AddItemToObject(json, "stations", stations_array);
            cJSON_AddNumberToObject(json, "total", stations.size());
            
            return json;
        });
}

// ========== HOW TO ADD TO YOUR PROJECT ==========
// 
// In main/mcp_server.cc, find McpServer::AddCommonTools()
// Add this line near the end (after existing tools):
//
// void McpServer::AddCommonTools() {
//     // ... existing tools ...
//     
//     // Add Radio Control Tools
//     AddRadioTools();  // <- Add this line
// }
//
// Don't forget to declare AddRadioTools() in mcp_server.h:
// private:
//     void AddRadioTools();
