cmake_minimum_required(VERSION 3.16)
project(retro_fps_engine_and_tooling LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(HEADLESS_TEST "Build the console-only headless telemetry test instead of the raylib window" ON)

# ---------------------------------------------------------------- core library
add_library(retro_core STATIC
    core_c/src/memory_arena.c
    core_c/src/state_machine.c
    core_c/src/combat_combo.c
)
target_include_directories(retro_core PUBLIC core_c/include)

if(MSVC)
    target_compile_options(retro_core PRIVATE /W4)
else()
    target_compile_options(retro_core PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(retro_core PUBLIC m)
endif()

enable_testing()

# ---------------------------------------------------------------- unit tests
add_executable(core_unit_tests core_c/tests/test_core.c)
target_link_libraries(core_unit_tests PRIVATE retro_core)
add_test(NAME core_unit_tests COMMAND core_unit_tests)

# ---------------------------------------------------------------- prototype
if(HEADLESS_TEST)
    add_executable(headless_test core_c/src/main_prototype.c)
    target_compile_definitions(headless_test PRIVATE HEADLESS_TEST)
    target_link_libraries(headless_test PRIVATE retro_core)
    add_test(NAME headless_telemetry COMMAND headless_test)
else()
    find_package(raylib 5.0 QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(raylib)
    endif()
    add_executable(retro_fps_prototype core_c/src/main_prototype.c)
    target_link_libraries(retro_fps_prototype PRIVATE retro_core raylib)
    if(APPLE)
        target_link_libraries(retro_fps_prototype PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
    endif()
endif()
