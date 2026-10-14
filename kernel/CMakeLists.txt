add_executable(kernel
  core/main.cpp
  core/boot_info.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/kernel_entry.S)
target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR}/arch/x86_64/include)
target_link_libraries(kernel PRIVATE core abi)
target_link_options(kernel PRIVATE
  -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
  -Wl,-z,max-page-size=4096)
set_target_properties(kernel PROPERTIES
  OUTPUT_NAME kernel SUFFIX .elf
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
