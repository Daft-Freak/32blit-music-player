cmake_minimum_required(VERSION 3.8.0)
project(music-player)
set(32BLIT_PATH "../" CACHE PATH "Path to 32blit.cmake")

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")

#add_definitions("-DPROFILER")

include(${32BLIT_PATH}/32blit.cmake)

blit_executable(music-player
    control-icons.cpp
    file-browser.cpp
    mp3-stream.cpp
    music-player.cpp
    vorbis-stream.cpp
)

blit_assets_yaml(music-player assets.yml)
blit_metadata(music-player metadata.yml)