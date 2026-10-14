add_library(acpi STATIC src/parse.cpp)
target_include_directories(acpi PUBLIC include)
target_link_libraries(acpi PUBLIC core)
