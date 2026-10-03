cmake_minimum_required(VERSION 3.14)
project(lumen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Emit build/compile_commands.json so clangd can parse the real flags instead of
# guessing an include path (a plain "json.hpp" include needs one).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# nlohmann/json - header only, vendored
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Embed the language keyword data (data/keywords.json) into the binary via a
# generated include, so the installed executable is self-contained.
add_executable(gen_keywords tools/gen_keywords.cpp)
set(KEYWORDS_INC ${CMAKE_CURRENT_BINARY_DIR}/keywords_data.inc)
add_custom_command(
    OUTPUT ${KEYWORDS_INC}
    COMMAND gen_keywords ${CMAKE_CURRENT_SOURCE_DIR}/data/keywords.json ${KEYWORDS_INC}
    DEPENDS gen_keywords ${CMAKE_CURRENT_SOURCE_DIR}/data/keywords.json
    COMMENT "Embedding language keyword data"
)
add_custom_target(keywords_data DEPENDS ${KEYWORDS_INC})

add_executable(lumen
    src/main.cpp
    src/terminal.cpp
    src/input.cpp
    src/ui.cpp
    src/markdown.cpp
    src/keywords.cpp
    src/ollama.cpp
    src/session.cpp
    src/tools.cpp
    src/config.cpp
    src/agent.cpp
    src/util.cpp
)
add_dependencies(lumen keywords_data)

# The binary dir is on the include path only so src/keywords.cpp can include the
# generated keywords_data.inc; the rest of the tree compiles without it.
target_include_directories(lumen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
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
