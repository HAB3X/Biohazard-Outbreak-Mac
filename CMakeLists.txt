cmake_minimum_required(VERSION 3.20)
project(biohazard_outbreak_mac VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

# The runtime: its folder in this repository, or a checkout of its own next to it
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ps2-native-runtime/CMakeLists.txt)
    set(PS2_RUNTIME_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR}/ps2-native-runtime)
else()
    set(PS2_RUNTIME_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR}/../ps2-native-runtime)
endif()
set(PS2_RUNTIME_DIR ${PS2_RUNTIME_DEFAULT} CACHE PATH "ps2-native-runtime's folder")
add_subdirectory(${PS2_RUNTIME_DIR} ps2-native-runtime)

# The partners' AI (src/partner_ai.h), both games'
set(PARTNER_AI_SOURCES src/partner_ai.cpp src/partner_ai_world.cpp src/partner_ai_telemetry.cpp)

# The keyboard and mouse: the keys' modes and the pointer over the games' menus (src/controls.h, src/pointer.h, src/menus.h)
set(CONTROL_SOURCES src/controls.cpp src/pointer.cpp src/text_map.cpp src/menus.cpp src/menus_title.cpp src/menus_save.cpp src/menus_net.cpp src/menus_play.cpp src/menus_select.cpp)

# Your own disc images (Japan), either or both: each game is its own program
set(FILE1_DISC "" CACHE FILEPATH "Your Biohazard Outbreak (Japan, SLPM-65428) disc image")
set(FILE2_DISC "" CACHE FILEPATH "Your Biohazard Outbreak File 2 (Japan, SLPM-65692) disc image")
if(NOT FILE1_DISC AND NOT FILE2_DISC)
    message(FATAL_ERROR "Set FILE1_DISC and/or FILE2_DISC to your disc images: cmake -DFILE2_DISC=/path/to/game.iso ...")
endif()

if(FILE1_DISC)
    ps2_recompile(file1_code CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/file1.toml DISC ${FILE1_DISC} EXECUTABLE SLPM_654.28
                  FILES 64)
    add_executable(biohazard-file1 src/file1.cpp ${CONTROL_SOURCES} src/tsnd.cpp src/network.cpp src/loadhigh.cpp src/usbsrch.cpp
                   src/avewrap.cpp src/typing.cpp ${PARTNER_AI_SOURCES})
    target_link_libraries(biohazard-file1 PRIVATE file1_code ps2runtime)
endif()

if(FILE2_DISC)
    ps2_recompile(file2_code CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/file2.toml DISC ${FILE2_DISC} EXECUTABLE SLPM_656.92
                  FILES 64)
    add_executable(biohazard-file2 src/file2.cpp ${CONTROL_SOURCES} src/tsnd.cpp src/network.cpp src/loadhigh.cpp src/usbsrch.cpp src/avewrap.cpp src/typing.cpp
                   ${PARTNER_AI_SOURCES})
    target_link_libraries(biohazard-file2 PRIVATE file2_code ps2runtime)
endif()
