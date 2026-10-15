add_library(acpi STATIC src/parse.cpp src/aml.cpp src/pci_route.cpp)
target_include_directories(acpi PUBLIC include)
target_link_libraries(acpi PUBLIC core)
