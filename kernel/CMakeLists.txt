add_executable(kernel
  core/main.cpp
  core/boot_info.cpp
  core/cmdline.cpp
  core/ktest.cpp
  core/log.cpp
  core/panic.cpp
  mm/heap.cpp
  mm/pmm.cpp
  mm/vmm.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/kernel_entry.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/gdt.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/idt.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/isr.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/paging.cpp)
target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR}/arch/x86_64/include)
target_link_libraries(kernel PRIVATE core mm abi)
target_link_options(kernel PRIVATE
  -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
  -Wl,-z,max-page-size=4096)
set_target_properties(kernel PROPERTIES
  OUTPUT_NAME kernel SUFFIX .elf
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
