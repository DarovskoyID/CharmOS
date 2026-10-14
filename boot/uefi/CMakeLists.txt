add_executable(bootx64 main.cpp memmap.cpp enter_kernel.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/paging.cpp)
target_include_directories(bootx64 PRIVATE ${CMAKE_SOURCE_DIR}/arch/x86_64/include)
target_link_libraries(bootx64 PRIVATE core elf abi)
set_target_properties(bootx64 PROPERTIES OUTPUT_NAME BOOTX64 SUFFIX .efi)
