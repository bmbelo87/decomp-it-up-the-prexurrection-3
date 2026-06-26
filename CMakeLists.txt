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
    src/result.c
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

add_executable(test_step tools/test_step.c src/step.c src/zlibinflate.c)
target_include_directories(test_step PRIVATE include)

add_executable(dump_bga2 tools/dump_bga2.c)
add_executable(dump_kf tools/dump_kf.c)
add_executable(dump_df tools/dump_df.c src/df_resource.c)
target_include_directories(dump_df PRIVATE include)
target_link_libraries(dump_df kernel32)

# Assets (BGA, AUDIO, STEP, Stage.cfg) são fornecidos pelo usuário com os arquivos originais do jogo.