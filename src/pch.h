#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstring>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

// Socket headers
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
	typedef SOCKET socket_t;
	#define SOCKET_ERROR_CODE SOCKET_ERROR
	#define CLOSE_SOCKET(s) closesocket(s)
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	typedef int socket_t;
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR_CODE -1
	#define CLOSE_SOCKET(s) close(s)
#endif