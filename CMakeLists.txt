cmake_minimum_required(VERSION 3.14)
project(lumen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json - header only, vendored
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(lumen
    src/main.cpp
    src/terminal.cpp
    src/input.cpp
    src/ui.cpp
    src/ollama.cpp
    src/session.cpp
    src/tools.cpp
    src/config.cpp
    src/agent.cpp
    src/util.cpp
)

target_include_directories(lumen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lumen PRIVATE
    CURL::libcurl
    Threads::Threads
    nlohmann_json
)

# Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lumen PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Install
install(TARGETS lumen RUNTIME DESTINATION bin)
