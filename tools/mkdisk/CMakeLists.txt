add_executable(mkdisk mkdisk.cpp)
target_link_libraries(mkdisk PRIVATE fs sysarea)
