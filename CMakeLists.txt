cmake_minimum_required(VERSION 3.28)
project(order-book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++ modules planned; scanning injects response files that break
# configure-only clang-tidy runs (see docs/decisions.md).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

option(OB_FUZZER "Build the libFuzzer targets (clang only)" OFF)
if(OB_FUZZER)
    # Instruments every translation unit in this build tree so the fuzzer gets
    # coverage feedback through the parser, not just through its own entry point.
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()

set(OB_SANITIZE "" CACHE STRING "Sanitizer preset: asan, ubsan, tsan, or empty")
if(OB_SANITIZE STREQUAL "asan")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
elseif(OB_SANITIZE STREQUAL "ubsan")
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
    add_link_options(-fsanitize=undefined)
elseif(OB_SANITIZE STREQUAL "tsan")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

enable_testing()

add_subdirectory(itch)
add_subdirectory(book)
add_subdirectory(engine)
add_subdirectory(queue)
add_subdirectory(bench)
add_subdirectory(tools)
add_subdirectory(fuzz)
add_subdirectory(tests)
