add_library(elf STATIC src/elf64.cpp)
target_include_directories(elf PUBLIC include)
target_link_libraries(elf PUBLIC core)
