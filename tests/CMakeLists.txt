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
)
target_link_libraries(ob_tests PRIVATE
    ob_itch ob_book ob_engine ob_queue ob_bench
    GTest::gtest_main
)
target_compile_definitions(ob_tests PRIVATE
    OB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

include(GoogleTest)
gtest_discover_tests(ob_tests DISCOVERY_TIMEOUT 60)
