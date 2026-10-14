add_library(mm STATIC src/frame_bitmap.cpp src/heap.cpp)
target_include_directories(mm PUBLIC include)
target_link_libraries(mm PUBLIC core)
