add_library(wm_client STATIC src/window.cpp)
target_include_directories(wm_client PUBLIC include)
target_link_libraries(wm_client PUBLIC c input)
