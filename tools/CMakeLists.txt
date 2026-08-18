add_executable(ob_slice slice.cpp)

add_executable(ob_fuzz_seed fuzz_seed.cpp)
target_link_libraries(ob_fuzz_seed PRIVATE ob_itch)
