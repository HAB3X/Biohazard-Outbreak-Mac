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
set(PARTNER_AI_SOURCES src/partner_ai.cpp src/partner_ai_brain.cpp src/partner_ai_world.cpp src/partner_ai_telemetry.cpp)
# The community mods (src/mods.h): the Modern and first-person cameras and the cheats
set(MODS_SOURCES src/mods.cpp src/mod_settings.cpp src/game_pad.cpp src/cheats.cpp src/first_person.cpp src/modern_camera.cpp src/modern_camera_config.cpp src/modern_camera_game.cpp
                 src/modern_camera_rig.cpp)

# The partners' AI decisions' tests (no disc needed): ctest, or build/partner_ai_brain_test
enable_testing()
add_executable(partner_ai_brain_test tests/partner_ai_brain_test.cpp src/partner_ai_brain.cpp)
target_include_directories(partner_ai_brain_test PRIVATE src ${PS2_RUNTIME_DIR}/runtime/include)
add_test(NAME partner_ai_brain COMMAND partner_ai_brain_test)
# The Modern camera's rig (no disc needed)
add_executable(modern_camera_rig_test tests/modern_camera_rig_test.cpp src/modern_camera_rig.cpp)
target_include_directories(modern_camera_rig_test PRIVATE src)
add_test(NAME modern_camera_rig COMMAND modern_camera_rig_test)

# The keyboard and mouse: the keys' modes and the pointer over the games' menus (src/controls.h, src/pointer.h, src/menus.h)
set(CONTROL_SOURCES src/controls.cpp src/pointer.cpp src/text_map.cpp src/menus.cpp src/menus_title.cpp src/menus_save.cpp src/menus_net.cpp src/menus_lobby.cpp src/menus_play.cpp src/menus_select.cpp src/menus_collection.cpp src/menus_keyboard.cpp)

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
                   src/avewrap.cpp src/lobbytrace.cpp src/typing.cpp ${PARTNER_AI_SOURCES} ${MODS_SOURCES})
    target_link_libraries(biohazard-file1 PRIVATE file1_code ps2runtime)
endif()

if(FILE2_DISC)
    ps2_recompile(file2_code CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/file2.toml DISC ${FILE2_DISC} EXECUTABLE SLPM_656.92
                  FILES 64)
    add_executable(biohazard-file2 src/file2.cpp ${CONTROL_SOURCES} src/tsnd.cpp src/network.cpp src/loadhigh.cpp src/usbsrch.cpp src/avewrap.cpp src/lobbytrace.cpp src/typing.cpp
                   ${PARTNER_AI_SOURCES} ${MODS_SOURCES})
    target_link_libraries(biohazard-file2 PRIVATE file2_code ps2runtime)
endif()
