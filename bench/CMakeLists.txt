add_library(ob_bench STATIC src/clock.cpp src/result.cpp)
target_include_directories(ob_bench PUBLIC include)

if(NOT OB_SANITIZE STREQUAL "")
    target_compile_definitions(ob_bench PRIVATE OB_SANITIZER_NAME="${OB_SANITIZE}")
endif()

# While bench/designated_machine.txt is absent, no machine is designated and
# every result is stamped DEV-ONLY. The file is baked in at configure time so
# the rule holds mechanically, not by memory.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/designated_machine.txt)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/designated_machine.txt OB_MACHINE_ID)
    string(STRIP "${OB_MACHINE_ID}" OB_MACHINE_ID)
    target_compile_definitions(ob_bench PRIVATE OB_DESIGNATED_MACHINE="${OB_MACHINE_ID}")
endif()

add_executable(ob_bench_demo src/main.cpp)
target_link_libraries(ob_bench_demo PRIVATE ob_bench)
