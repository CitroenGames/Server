#pragma once

// Track information structure
struct TrackInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    int duration;  // in seconds
    std::string filepath;
    std::string description_path;
};