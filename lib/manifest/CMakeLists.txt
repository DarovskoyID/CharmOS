add_library(manifest STATIC src/manifest.cpp)
target_include_directories(manifest PUBLIC include)
target_link_libraries(manifest PUBLIC core)
