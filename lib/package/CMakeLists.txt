add_library(package STATIC src/package.cpp)
target_include_directories(package PUBLIC include)
target_link_libraries(package PUBLIC core crypto abi)
