#pragma once

// Server configuration - One day we will have a config file for this but for now will just hard code it into the executeable :/
const int PORT = 8080;
const size_t BUFFER_SIZE = 8192;
const std::u8string MUSIC_DIR = u8"music/";  
const std::u8string DESCRIPTION_EXT = u8".json";