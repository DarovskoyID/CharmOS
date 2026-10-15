add_library(sysarea STATIC src/sysarea.cpp)
target_include_directories(sysarea PUBLIC include)
target_link_libraries(sysarea PUBLIC core crypto fs)
