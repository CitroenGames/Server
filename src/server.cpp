#include "pch.h"
#include "ServerConfig.h"
#include "TrackInfo.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Global variables
std::unordered_map<std::string, TrackInfo> track_catalog;
std::mutex catalog_mutex;

// Function to initialize socket system on Windows
bool initializeSocketSystem() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

// Function to clean up socket system on Windows
void cleanupSocketSystem() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Function to URL-decode a string
std::string urlDecode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '%') {
            if (i + 2 < value.length()) {
                std::string hex = value.substr(i + 1, 2);
                char dec = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                decoded += dec;
                i += 2;
            } else {
                // Malformed encoding, just append '%'
                decoded += '%';
            }
        } else if (value[i] == '+') {
            decoded += ' ';
        } else {
            decoded += value[i];
        }
    }
    return decoded;
}

// Function to load track catalog from the music directory
void loadTrackCatalog() {
    std::lock_guard<std::mutex> lock(catalog_mutex);
    track_catalog.clear();

    try {
        if (!fs::exists(MUSIC_DIR)) {
            fs::create_directory(MUSIC_DIR);
            std::cout << "Created music directory: " << MUSIC_DIR << std::endl;
            return;
        }

        for (const auto& entry : fs::directory_iterator(MUSIC_DIR)) {
            if (entry.path().extension() == ".mp3") {
                std::string filepath = entry.path().string();
                std::string filename = entry.path().filename().string();
                std::string id = filename.substr(0, filename.length() - 4);  // Remove .mp3
                std::string desc_path = MUSIC_DIR + id + DESCRIPTION_EXT;

                TrackInfo track;
                track.id = id;
                track.filepath = filepath;
                track.description_path = desc_path;

                // Default values in case description file doesn't exist
                track.title = id;
                track.artist = "Unknown";
                track.album = "Unknown";
                track.duration = 0;

                // Try to load description file if it exists
                if (fs::exists(desc_path)) {
                    std::ifstream desc_file(desc_path);
                    if (desc_file.is_open()) {
                        try {
                            json desc_data = json::parse(desc_file);
                            track.title = desc_data.value("title", id);
                            track.artist = desc_data.value("artist", "Unknown");
                            track.album = desc_data.value("album", "Unknown");
                            track.duration = desc_data.value("duration", 0);
                        } catch (const std::exception& e) {
                            std::cerr << "Error parsing " << desc_path << ": " << e.what() << std::endl;
                        }
                        desc_file.close();
                    }
                } else {
                    // Create a default description file
                    json desc_data;
                    desc_data["title"] = id;
                    desc_data["artist"] = "Unknown";
                    desc_data["album"] = "Unknown";
                    desc_data["duration"] = 0;

                    std::ofstream desc_file(desc_path);
                    if (desc_file.is_open()) {
                        desc_file << desc_data.dump(4);
                        desc_file.close();
                    } else {
                        std::cerr << "Failed to create description file: " << desc_path << std::endl;
                    }
                }

                track_catalog[id] = track;
                std::cout << "Loaded track: " << track.title << " (" << id << ")" << std::endl;
            }
        }

        std::cout << "Loaded " << track_catalog.size() << " tracks into catalog." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading track catalog: " << e.what() << std::endl;
    }
}

// Function to send HTTP response header
void sendHttpHeader(socket_t client_socket, int status_code, const std::string& content_type, size_t content_length) {
    std::string status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "Unknown"; break;
    }

    std::string header = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
    header += "Content-Type: " + content_type + "\r\n";
    header += "Content-Length: " + std::to_string(content_length) + "\r\n";
    header += "Connection: close\r\n";
    header += "Access-Control-Allow-Origin: *\r\n";  // Enable CORS
    header += "\r\n";  // End of header

    send(client_socket, header.c_str(), header.length(), 0);
}

// Function to send catalog as JSON response
void sendCatalog(socket_t client_socket) {
    json catalog_json = json::array();

    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        for (const auto& pair : track_catalog) {
            const TrackInfo& track = pair.second;
            json track_json;
            track_json["id"] = track.id;
            track_json["title"] = track.title;
            track_json["artist"] = track.artist;
            track_json["album"] = track.album;
            track_json["duration"] = track.duration;
            catalog_json.push_back(track_json);
        }
    }

    std::string response_body = catalog_json.dump();

    sendHttpHeader(client_socket, 200, "application/json", response_body.length());
    send(client_socket, response_body.c_str(), response_body.length(), 0);
}

// Function to send description file for a track
void sendTrackDescription(socket_t client_socket, const std::string& track_id) {
    std::lock_guard<std::mutex> lock(catalog_mutex);

    auto it = track_catalog.find(track_id);
    if (it == track_catalog.end()) {
        // Track not found
        std::string error_msg = "{\"error\": \"Track not found\"}";
        sendHttpHeader(client_socket, 404, "application/json", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        return;
    }

    const TrackInfo& track = it->second;
    if (!fs::exists(track.description_path)) {
        // Description file not found
        std::string error_msg = "{\"error\": \"Description file not found\"}";
        sendHttpHeader(client_socket, 404, "application/json", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        return;
    }

    std::ifstream desc_file(track.description_path, std::ios::binary);
    if (!desc_file.is_open()) {
        // Failed to open file
        std::string error_msg = "{\"error\": \"Failed to open description file\"}";
        sendHttpHeader(client_socket, 500, "application/json", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        return;
    }

    // Get file size
    desc_file.seekg(0, std::ios::end);
    size_t file_size = desc_file.tellg();
    desc_file.seekg(0, std::ios::beg);

    // Prepare and send HTTP header
    sendHttpHeader(client_socket, 200, "application/json", file_size);

    // Send file content
    char buffer[BUFFER_SIZE];
    while (desc_file) {
        desc_file.read(buffer, BUFFER_SIZE);
        size_t bytes_read = desc_file.gcount();
        if (bytes_read > 0) {
            send(client_socket, buffer, bytes_read, 0);
        }
    }

    desc_file.close();
}

// Function to send MP3 file data
void sendMp3File(socket_t client_socket, const std::string& track_id, int64_t start_pos = 0) {
    std::lock_guard<std::mutex> lock(catalog_mutex);

    auto it = track_catalog.find(track_id);
    if (it == track_catalog.end()) {
        // Track not found
        std::string error_msg = "Track not found";
        sendHttpHeader(client_socket, 404, "text/plain", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        return;
    }

    const TrackInfo& track = it->second;
    if (!fs::exists(track.filepath)) {
        // MP3 file not found
        std::string error_msg = "MP3 file not found";
        sendHttpHeader(client_socket, 404, "text/plain", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        return;
    }

    std::ifstream mp3_file(track.filepath, std::ios::binary);
    if (!mp3_file.is_open()) {
        // Failed to open file
        std::string error_msg = "Failed to open MP3 file";
        sendHttpHeader(client_socket, 500, "text/plain", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        return;
    }

    // Get file size
    mp3_file.seekg(0, std::ios::end);
    size_t file_size = mp3_file.tellg();

    // Set position based on Range header
    start_pos = std::max<int64_t>(0, std::min<int64_t>(start_pos, file_size));
    mp3_file.seekg(start_pos, std::ios::beg);

    // Prepare and send HTTP header for MP3
    sendHttpHeader(client_socket, 200, "audio/mpeg", file_size - start_pos);

    // Stream the file
    char buffer[BUFFER_SIZE];
    while (mp3_file) {
        mp3_file.read(buffer, BUFFER_SIZE);
        size_t bytes_read = mp3_file.gcount();
        if (bytes_read > 0) {
            send(client_socket, buffer, bytes_read, 0);
        }
    }

    mp3_file.close();
}

// Function to handle HTTP requests
void handleHttpRequest(socket_t client_socket) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // Receive HTTP request
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        CLOSE_SOCKET(client_socket);
        return;
    }

    // Parse HTTP request
    std::string request(buffer);
    std::string method, path, version;
    std::istringstream request_stream(request);
    request_stream >> method >> path >> version;

    std::cout << "Request: " << method << " " << path << std::endl;

    // Parse Range header if present
    int64_t range_start = 0;
    size_t range_pos = request.find("Range: bytes=");
    if (range_pos != std::string::npos) {
        size_t start_pos = range_pos + 13;  // Length of "Range: bytes="
        size_t end_pos = request.find('-', start_pos);
        if (end_pos != std::string::npos) {
            std::string range_start_str = request.substr(start_pos, end_pos - start_pos);
            range_start = std::stoll(range_start_str);
            std::cout << "Range request starting at: " << range_start << std::endl;
        }
    }

    // Handle different paths
    if (path == "/catalog") {
        // Return the catalog of available tracks
        sendCatalog(client_socket);
    } else if (path.find("/description/") == 0) {
        // Return the description file for a specific track
        std::string encoded_track_id = path.substr(13);  // Remove "/description/"
        std::string track_id = urlDecode(encoded_track_id); // Decode the track ID
        sendTrackDescription(client_socket, track_id);
    } else if (path.find("/stream/") == 0) {
        // Stream the MP3 file for a specific track
        std::string encoded_track_id = path.substr(8);  // Remove "/stream/"
        std::string track_id = urlDecode(encoded_track_id); // Decode the track ID
        sendMp3File(client_socket, track_id, range_start);
    } else if (path == "/reload") {
        // Reload the track catalog
        loadTrackCatalog();
        std::string response = "{\"status\": \"Catalog reloaded\"}";
        sendHttpHeader(client_socket, 200, "application/json", response.length());
        send(client_socket, response.c_str(), response.length(), 0);
    } else {
        // Path not found
        std::string error_msg = "Not Found";
        sendHttpHeader(client_socket, 404, "text/plain", error_msg.length());
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
    }

    CLOSE_SOCKET(client_socket);
}

// Function to handle client connections in a separate thread
void clientHandlerThread(socket_t client_socket) {
    handleHttpRequest(client_socket);
}

int main() {
    // Initialize socket system on Windows
    if (!initializeSocketSystem()) {
        std::cerr << "Failed to initialize socket system." << std::endl;
        return 1;
    }

    // Create server socket
    socket_t server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket." << std::endl;
        cleanupSocketSystem();
        return 1;
    }

    // Enable socket reuse option
    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR_CODE) {
        std::cerr << "Failed to set socket options." << std::endl;
        CLOSE_SOCKET(server_socket);
        cleanupSocketSystem();
        return 1;
    }

    // Bind socket to address and port
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR_CODE) {
        std::cerr << "Failed to bind socket to port " << PORT << std::endl;
        CLOSE_SOCKET(server_socket);
        cleanupSocketSystem();
        return 1;
    }

    // Listen for incoming connections
    if (listen(server_socket, 10) == SOCKET_ERROR_CODE) {
        std::cerr << "Failed to listen on socket." << std::endl;
        CLOSE_SOCKET(server_socket);
        cleanupSocketSystem();
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;
    std::cout << "Loading track catalog..." << std::endl;
    loadTrackCatalog();

    // Main server loop
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        socket_t client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Failed to accept client connection." << std::endl;
            continue;
        }

        // Get client IP address
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected: " << client_ip << std::endl;

        // Handle client in a separate thread
        std::thread(clientHandlerThread, client_socket).detach();
    }

    // Clean up (this part will not be reached in practice)
    CLOSE_SOCKET(server_socket);
    cleanupSocketSystem();

    return 0;
}