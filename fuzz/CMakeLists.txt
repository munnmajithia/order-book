if(NOT OB_FUZZ)
    return()
endif()

add_executable(ob_itch_fuzz itch_parse_fuzzer.cpp)
target_link_libraries(ob_itch_fuzz PRIVATE ob_itch)
target_compile_options(ob_itch_fuzz PRIVATE -fsanitize=fuzzer)
target_link_options(ob_itch_fuzz PRIVATE -fsanitize=fuzzer)
