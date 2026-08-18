# The fuzz entry point is built in every preset via the replay driver; the
# libFuzzer binary itself needs clang and is gated behind OB_FUZZER.
add_library(ob_fuzz_entry STATIC framing_fuzzer.cpp)
target_link_libraries(ob_fuzz_entry PUBLIC ob_itch)

add_executable(ob_fuzz_replay replay_main.cpp)
target_link_libraries(ob_fuzz_replay PRIVATE ob_fuzz_entry)

if(OB_FUZZER)
    add_executable(ob_framing_fuzzer framing_fuzzer.cpp)
    target_link_libraries(ob_framing_fuzzer PRIVATE ob_itch)
    target_link_options(ob_framing_fuzzer PRIVATE -fsanitize=fuzzer)
endif()
