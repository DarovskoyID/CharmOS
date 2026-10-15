add_executable(sign sign.cpp pack.cpp)
target_link_libraries(sign PRIVATE package)
