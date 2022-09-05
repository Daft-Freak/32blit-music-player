# Basic parameters; check that these match your project / environment
cmake_minimum_required(VERSION 3.9)

project(music-player)

set(PROJECT_SOURCE
    mp3-stream.cpp
    music-player.cpp
    vgm-stream.cpp
    vorbis-stream.cpp
)
set(PROJECT_DISTRIBS LICENSE README.md)

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")

#add_definitions("-DPROFILER")

# Build configuration; approach this with caution!
if(MSVC)
  add_compile_options("/W4" "/wd4244" "/wd4324" "/wd4458" "/wd4100")
else()
  add_compile_options("-Wall" "-Wextra" "-Wdouble-promotion" "-Wno-unused-parameter")
endif()

find_package (32BLIT CONFIG REQUIRED PATHS ../32blit-sdk)
add_subdirectory(DUH)

# libvgm, but avoid top-level CMakeLists
include(libvgm/cmake/utils.cmake)
set(LIBVGM_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libvgm)

add_definitions("-D HAVE_STDINT_H")

set(DISABLED_WARNINGS
    -Wno-double-promotion
    -Wno-sign-compare
    -Wno-type-limits
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-unused-variable
)

set(SNDEMU__ALL OFF)
set(SNDEMU_SN76496_ALL ON)
#set(SNDEMU_YM2413_ALL ON) # tables
#set(SNDEMU_YM2612_ALL ON) # tables
set(SNDEMU_YM2612_GPGX ON)
#set(SNDEMU_YM2612_GENS ON)
set(SNDEMU_YM2612_NUKED ON)
#set(SNDEMU_YM2151_ALL ON)
set(SNDEMU_SEGAPCM_ALL ON)
set(SNDEMU_RF5C68_ALL ON)
set(SNDEMU_YM2203_ALL ON)
set(SNDEMU_YM2608_ALL ON)
set(SNDEMU_YM2610_ALL ON)
#set(SNDEMU_YM3812_ALL ON)
#set(SNDEMU_YM3526_ALL ON)
#set(SNDEMU_Y8950_ALL ON)
#set(SNDEMU_YMF262_ALL ON)
#set(SNDEMU_YMF278B_ALL ON) # ymf278b.c vol_tab
set(SNDEMU_YMZ280B_ALL ON)
set(SNDEMU_YMF271_ALL ON)
set(SNDEMU_AY8910_ALL ON)
set(SNDEMU_32X_PWM_ALL ON)
set(SNDEMU_GAMEBOY_ALL ON)
#set(SNDEMU_NESAPU_ALL ON) # tables
#set(SNDEMU_NESAPU_MAME ON) # ...
set(SNDEMU_NESAPU_NSFPLAY ON)
set(SNDEMU_NESAPU_FDS ON)
#set(SNDEMU_YMW258_ALL ON) multipcm.c tables
set(SNDEMU_UPD7759_ALL ON)
set(SNDEMU_OKIM6258_ALL ON)
set(SNDEMU_OKIM6295_ALL ON)
set(SNDEMU_K051649_ALL ON)
set(SNDEMU_K054539_ALL ON)
# set(SNDEMU_C6280_ALL ON) #large tables
set(SNDEMU_C140_ALL ON)
set(SNDEMU_C219_ALL ON)
set(SNDEMU_K053260_ALL ON)
set(SNDEMU_POKEY_ALL ON)
set(SNDEMU_QSOUND_ALL ON)
#set(SNDEMU_SCSP_ALL ON) # type conflict
set(SNDEMU_WSWAN_ALL ON)
set(SNDEMU_VBOY_VSU_ALL ON)
set(SNDEMU_SAA1099_ALL ON)
set(SNDEMU_ES5503_ALL ON)
set(SNDEMU_ES5506_ALL ON)
set(SNDEMU_X1_010_ALL ON)
set(SNDEMU_C352_ALL ON)
set(SNDEMU_GA20_ALL ON)

add_subdirectory(libvgm/emu EXCLUDE_FROM_ALL)
add_subdirectory(libvgm/player EXCLUDE_FROM_ALL)
add_library(vgm-utils STATIC libvgm/utils/DataLoader.c cpconv_stub.c)

target_compile_options(vgm-emu PRIVATE ${DISABLED_WARNINGS} -Wno-incompatible-pointer-types)
target_compile_options(vgm-player PRIVATE ${DISABLED_WARNINGS})

# executable
blit_executable (${PROJECT_NAME} ${PROJECT_SOURCE})
blit_assets_yaml (${PROJECT_NAME} assets.yml)
blit_metadata (${PROJECT_NAME} metadata.yml)
target_link_libraries (${PROJECT_NAME} DUH vgm-emu vgm-player)
add_custom_target (flash DEPENDS ${PROJECT_NAME}.flash)

# setup release packages
install (FILES ${PROJECT_DISTRIBS} DESTINATION .)
set (CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set (CPACK_GENERATOR "ZIP" "TGZ")
include (CPack)
