include(FetchContent)

# Pinned by release-asset tarball + SHA256: asset bytes are immutable and the
# checksum survives upstream tag moves (see docs/decisions.md).
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ob_tests
    smoke_test.cpp
    histogram_test.cpp
    bench_result_test.cpp
    itch_messages_test.cpp
    reader_fixture_test.cpp
    decoder_census_test.cpp
    malformed_input_test.cpp
    reference_book_test.cpp
    book_invariants_test.cpp
    book_property_test.cpp
    replay_invariants_test.cpp
    fast_book_test.cpp
    fast_book_xval_test.cpp
    golden_snapshot_test.cpp
)
target_link_libraries(ob_tests PRIVATE
    ob_itch ob_book ob_engine ob_queue ob_bench ob_fuzz_entry
    GTest::gtest_main
)
target_include_directories(ob_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ob_tests PRIVATE
    OB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

include(GoogleTest)
gtest_discover_tests(ob_tests DISCOVERY_TIMEOUT 60)
# Seed a corpus from the fixture and replay it through the fuzz entry point.
# libFuzzer is clang-only, but this runs the same code in every preset, so the
# fuzz path is covered by ASan, UBSan and TSan too.
set(OB_FUZZ_CORPUS "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus")
add_test(NAME FuzzCorpus.Seed
    COMMAND ob_fuzz_seed "${CMAKE_CURRENT_SOURCE_DIR}/data/fixture.itch" "${OB_FUZZ_CORPUS}")
set_tests_properties(FuzzCorpus.Seed PROPERTIES FIXTURES_SETUP ob_fuzz_corpus)
add_test(NAME FuzzCorpus.Replay COMMAND ob_fuzz_replay "${OB_FUZZ_CORPUS}")
set_tests_properties(FuzzCorpus.Replay PROPERTIES FIXTURES_REQUIRED ob_fuzz_corpus)
