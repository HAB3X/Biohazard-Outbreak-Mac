cmake_minimum_required(VERSION 3.20)
project(biohazard_outbreak_mac VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
include_directories(src)

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

# The Resident Evil 4 AI routines (src/re4/re4_ai.h)
set(RE4_SOURCES src/re4/re4_ai.cpp)

# The partners' AI decisions' tests (no disc needed): ctest, or build/partner_ai_brain_test
enable_testing()
add_executable(partner_ai_brain_test tests/partner_ai_brain_test.cpp src/partner_ai_brain.cpp)
target_include_directories(partner_ai_brain_test PRIVATE src ${PS2_RUNTIME_DIR}/runtime/include)
add_test(NAME partner_ai_brain COMMAND partner_ai_brain_test)
# The Modern camera's rig (no disc needed)
add_executable(modern_camera_rig_test tests/modern_camera_rig_test.cpp src/modern_camera_rig.cpp)
target_include_directories(modern_camera_rig_test PRIVATE src)
add_test(NAME modern_camera_rig COMMAND modern_camera_rig_test)
# The RE4 enemy AI tests (no disc needed)
add_executable(re4_ai_test tests/re4_ai_test.cpp src/re4/re4_ai.cpp)
target_include_directories(re4_ai_test PRIVATE src ${PS2_RUNTIME_DIR}/runtime/include)
add_test(NAME re4_ai COMMAND re4_ai_test)

# The keyboard and mouse: the keys' modes and the pointer over the games' menus (src/controls.h, src/pointer.h, src/menus.h)
set(CONTROL_SOURCES src/controls.cpp src/pointer.cpp src/text_map.cpp src/menus.cpp src/menus_title.cpp src/menus_save.cpp src/menus_net.cpp src/menus_lobby.cpp src/menus_play.cpp src/menus_select.cpp src/menus_collection.cpp src/menus_keyboard.cpp)

# Your own disc images (Japan), either or both: each game is its own program
set(FILE1_DISC "" CACHE FILEPATH "Your Biohazard Outbreak (Japan, SLPM-65428) disc image")
set(FILE2_DISC "" CACHE FILEPATH "Your Biohazard Outbreak File 2 (Japan, SLPM-65692) disc image")
if(NOT FILE1_DISC AND NOT FILE2_DISC)
    message(FATAL_ERROR "Set FILE1_DISC and/or FILE2_DISC to your disc images: cmake -DFILE2_DISC=/path/to/game.iso ...")
endif()

# File 2's enemies in File 1 (src/transplant.h): with both discs, File 2's AI for the kinds File 1 has none of, carried into File 1's
# memory (tools/bh_transplant.py) and recompiled with File 1 as its generated overlay "transplant"
set(TRANSPLANT_KINDS 11 14 34 42 46 48 54 62)
if(FILE1_DISC AND FILE2_DISC)
    set(transplant ${CMAKE_CURRENT_BINARY_DIR}/transplant)
    file(GLOB transplant_tools ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.py)
    add_custom_command(
        OUTPUT ${transplant}/transplant.bin ${transplant}/transplant.json ${transplant}/transplant_blob.c
        COMMAND ${CMAKE_COMMAND} -E env FILE1_DISC=${FILE1_DISC} FILE2_DISC=${FILE2_DISC} PS2_RUNTIME_DIR=${PS2_RUNTIME_DIR}
                ${PS2_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bh_transplant.py build file2 file1 ${transplant} ${TRANSPLANT_KINDS}
        DEPENDS ${transplant_tools} ${CMAKE_CURRENT_SOURCE_DIR}/config/file1.toml ${CMAKE_CURRENT_SOURCE_DIR}/config/file2.toml
        COMMENT "Carrying File 2's enemy AI into File 1"
        VERBATIM)
    set(FILE1_GENERATED GENERATED ${transplant} GENERATED_DEPENDS ${transplant}/transplant.bin ${transplant}/transplant.json)
    set(FILE1_TRANSPLANT ${transplant}/transplant_blob.c)
endif()

if(FILE1_DISC)
    ps2_recompile(file1_code CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/file1.toml DISC ${FILE1_DISC} EXECUTABLE SLPM_654.28
                  FILES 64 ${FILE1_GENERATED})
    add_executable(biohazard-file1 src/file1.cpp ${CONTROL_SOURCES} src/tsnd.cpp src/network.cpp src/loadhigh.cpp src/usbsrch.cpp
                   src/avewrap.cpp src/lobbytrace.cpp src/typing.cpp ${PARTNER_AI_SOURCES} ${MODS_SOURCES} ${RE4_SOURCES} src/widescreen.cpp src/intro.cpp
                   src/transplant.cpp ${FILE1_TRANSPLANT})
    if(FILE1_TRANSPLANT)
        target_compile_definitions(biohazard-file1 PRIVATE TRANSPLANT=1)
    endif()
    target_link_libraries(biohazard-file1 PRIVATE file1_code ps2runtime)
endif()

if(FILE2_DISC)
    ps2_recompile(file2_code CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/file2.toml DISC ${FILE2_DISC} EXECUTABLE SLPM_656.92
                  FILES 64)
    add_executable(biohazard-file2 src/file2.cpp ${CONTROL_SOURCES} src/tsnd.cpp src/network.cpp src/loadhigh.cpp src/usbsrch.cpp src/avewrap.cpp src/lobbytrace.cpp src/typing.cpp
                   ${PARTNER_AI_SOURCES} ${MODS_SOURCES} ${RE4_SOURCES} src/widescreen.cpp src/intro.cpp)
    target_link_libraries(biohazard-file2 PRIVATE file2_code ps2runtime)
endif()
