#pragma once

// Track information structure
struct TrackInfo {
    std::u8string id;
    std::u8string title;
    std::u8string artist;
    std::u8string album;
    int duration;  // in seconds
    std::u8string filepath;
    std::u8string description_path;
};