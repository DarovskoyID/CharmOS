add_library(cap STATIC src/handle_table.cpp)
target_include_directories(cap PUBLIC include)
target_link_libraries(cap PUBLIC core)
