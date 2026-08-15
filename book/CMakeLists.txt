add_library(ob_book STATIC src/reference_book.cpp src/fast_book.cpp)
target_include_directories(ob_book PUBLIC include)
target_link_libraries(ob_book PUBLIC ob_itch)
