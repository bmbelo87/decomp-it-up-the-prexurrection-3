cmake_minimum_required(VERSION 3.10)
project(Pumpy C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN")

if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /O2 /GL /MT")
    add_definitions(-DWIN32 -D_WIN32 -D_WINDOWS)
endif()

add_executable(Pumpy WIN32
    src/main.c
    src/window.c
    src/warning.c
    src/logo.c
    src/menu.c
    src/song_select.c
    src/staff.c
    src/loading.c
    src/gameplay.c
    src/game_option.c
    src/render.c
    src/texture.c
    src/audio.c
    src/input.c
    src/font.c
    src/bga.c
    src/bga2_parser.c
    src/vsl.c
    src/df_resource.c
    src/util.c
    src/resource.c
    src/step.c
    src/song.c
    src/zlibinflate.c
)

target_include_directories(Pumpy PRIVATE include)

target_link_libraries(Pumpy
    opengl32 glu32 dsound winmm ole32
    kernel32 user32 gdi32 advapi32 shell32
)

add_executable(dump_bga2 tools/dump_bga2.c)
add_executable(dump_kf tools/dump_kf.c)
add_executable(dump_df tools/dump_df.c src/df_resource.c)
target_include_directories(dump_df PRIVATE include)
target_link_libraries(dump_df kernel32)

add_custom_command(TARGET Pumpy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Pumpy>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/../BGA"
        "$<TARGET_FILE_DIR:Pumpy>/assets/BGA"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/AUDIO"
        "$<TARGET_FILE_DIR:Pumpy>/assets/AUDIO"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/../STEP"
        "$<TARGET_FILE_DIR:Pumpy>/STEP"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/../Stage.cfg"
        "$<TARGET_FILE_DIR:Pumpy>/Stage.cfg"
    COMMENT "Copying game assets"
)