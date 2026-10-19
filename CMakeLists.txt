cmake_minimum_required(VERSION 3.20)
project(TisorNameSniper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Windows / MSVC flags ─────────────────────────────────────────────────────
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()
if(MSVC)
    add_compile_options(/utf-8 /MP /W3 /wd4996)
    # Enable all optimisations in Release
    string(REPLACE "/O2" "/Ox" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# ── FetchContent deps ─────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# FTXUI v5 ────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v5.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ftxui)

# cpr 1.10.5 (bundles curl; uses Schannel on Windows → no OpenSSL needed) ─────
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG        1.10.5
    GIT_SHALLOW    TRUE
)
set(CPR_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "" FORCE)
set(CPR_ENABLE_SSL      ON  CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL   ON  CACHE BOOL "" FORCE)   # Windows native TLS
FetchContent_MakeAvailable(cpr)

# nlohmann/json ────────────────────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(TisorNameSniper
    main.cpp
    Sniper.cpp
    ProxyManager.cpp
    Capsolver.cpp
    Generator.cpp
    Webhook.cpp
)

target_link_libraries(TisorNameSniper PRIVATE
    ftxui::component
    ftxui::dom
    ftxui::screen
    cpr::cpr
    nlohmann_json::nlohmann_json
)

if(WIN32 AND MSVC)
    set_target_properties(TisorNameSniper PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()

# Convenience: copy binary to repo root after build
add_custom_command(TARGET TisorNameSniper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:TisorNameSniper>
        ${CMAKE_SOURCE_DIR}/$<TARGET_FILE_NAME:TisorNameSniper>
    COMMENT "Copying binary to project root"
)
