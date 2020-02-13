cmake_minimum_required(VERSION 3.1.0)
set(32BLIT_PATH "../" CACHE PATH "Path to 32blit.cmake")
project(music-player)

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")

#add_definitions("-DPROFILER")
##
include_directories(
    ${32BLIT_PATH}/32blit-stm32/Drivers/STM32H7xx_HAL_Driver/Inc
    ${32BLIT_PATH}/32blit-stm32/Inc
    ${32BLIT_PATH}/32blit-stm32/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${32BLIT_PATH}/32blit-stm32/Drivers/CMSIS/Include
)
add_definitions(-DSTM32H750xx)
##

include(${32BLIT_PATH}/32blit.cmake)

blit_executable(music-player
    buffered-file.cpp
    file-browser.cpp
    mp3-stream.cpp
    music-player.cpp
)
