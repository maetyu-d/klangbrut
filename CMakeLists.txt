cmake_minimum_required(VERSION 3.22)
project(BlockMusicToy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BLOCK_MUSIC_FETCH_JUCE "Try FetchContent to download JUCE if not found." OFF)
set(BLOCK_MUSIC_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (folder containing CMakeLists.txt).")

if (BLOCK_MUSIC_JUCE_PATH)
    message(STATUS "Using JUCE from BLOCK_MUSIC_JUCE_PATH: ${BLOCK_MUSIC_JUCE_PATH}")
    add_subdirectory(${BLOCK_MUSIC_JUCE_PATH} JUCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using JUCE from ./JUCE")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/JUCE JUCE)
else()
    find_package(JUCE CONFIG QUIET)

    if (NOT JUCE_FOUND AND BLOCK_MUSIC_FETCH_JUCE)
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.3
        )
        FetchContent_MakeAvailable(JUCE)
    endif()

    if (NOT JUCE_FOUND AND NOT TARGET juce::juce_core)
        message(FATAL_ERROR
            "JUCE not found.\n"
            "Use one of:\n"
            "  1) -DBLOCK_MUSIC_JUCE_PATH=/path/to/JUCE\n"
            "  2) Add JUCE as ./JUCE in this repo\n"
            "  3) Install JUCE package and set JUCE_DIR/CMAKE_PREFIX_PATH\n"
            "  4) Enable download: -DBLOCK_MUSIC_FETCH_JUCE=ON")
    endif()
endif()

juce_add_gui_app(BlockMusicToy
    PRODUCT_NAME "Block Music Toy"
)
juce_generate_juce_header(BlockMusicToy)

target_sources(BlockMusicToy
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.h
        Source/MainComponent.cpp
        Source/World.h
        Source/World.cpp
        Source/SynthEngine.h
        Source/SynthEngine.cpp
)

target_compile_definitions(BlockMusicToy
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(BlockMusicToy
    PRIVATE
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
