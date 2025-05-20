cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
project(Server)

add_subdirectory(deps)

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.h")

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC src)

target_link_libraries(${PROJECT_NAME} nlohmann_json)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)