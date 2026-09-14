cmake_minimum_required(VERSION 3.16)
project(FastNote VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Dependencies: SDL3 + FreeType via pkg-config.
# Ubuntu/Debian: sudo apt install libsdl3-dev libfreetype-dev
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(FREETYPE REQUIRED freetype2)

set(FASTNOTE_SOURCES
    src/main.c
    src/app.c
    src/text_buffer.c
    src/editor.c
    src/font.c
    src/file_dialog.c
    src/filetype.c
    src/highlight.c
    src/recents.c
    src/project.c
    src/ui.c
    src/render.c
)

add_executable(fastnote ${FASTNOTE_SOURCES})

target_include_directories(fastnote PRIVATE src ${SDL3_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_directories(fastnote PRIVATE ${SDL3_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})
target_link_libraries(fastnote PRIVATE ${SDL3_LIBRARIES} ${FREETYPE_LIBRARIES})
target_compile_options(fastnote PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    $<$<C_COMPILER_ID:GNU>:-Wdouble-promotion>
)

option(FASTNOTE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(FASTNOTE_SANITIZERS)
    target_compile_options(fastnote PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(fastnote PRIVATE -fsanitize=address,undefined)
endif()

# Unit tests for the SDL-free modules (text buffer + editor logic).
add_executable(fastnote_tests
    tests/test_main.c
    src/text_buffer.c
    src/editor.c
    src/filetype.c
    src/highlight.c
    src/recents.c
)
target_include_directories(fastnote_tests PRIVATE src)
target_compile_options(fastnote_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_compile_options(fastnote_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(fastnote_tests PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME fastnote_tests COMMAND fastnote_tests)

# Integration test: real App event loop with synthetic SDL events.
add_executable(fastnote_itest
    tests/test_app.c
    src/app.c
    src/editor.c
    src/text_buffer.c
    src/font.c
    src/file_dialog.c
    src/filetype.c
    src/highlight.c
    src/recents.c
    src/project.c
    src/ui.c
    src/render.c
)
target_include_directories(fastnote_itest PRIVATE src ${SDL3_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_directories(fastnote_itest PRIVATE ${SDL3_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})
target_link_libraries(fastnote_itest PRIVATE ${SDL3_LIBRARIES} ${FREETYPE_LIBRARIES})
target_compile_options(fastnote_itest PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_compile_options(fastnote_itest PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(fastnote_itest PRIVATE -fsanitize=address,undefined)
add_test(NAME fastnote_itest COMMAND fastnote_itest)
