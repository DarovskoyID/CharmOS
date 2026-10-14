add_library(fs STATIC src/gpt.cpp src/fat32.cpp src/ext2.cpp)
target_include_directories(fs PUBLIC include)
target_link_libraries(fs PUBLIC core)
