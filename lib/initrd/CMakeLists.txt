add_library(initrd STATIC src/initrd.cpp)
target_include_directories(initrd PUBLIC include)
target_link_libraries(initrd PUBLIC core abi)
