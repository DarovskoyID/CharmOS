add_executable(kernel
  core/main.cpp
  core/boot_info.cpp
  core/cmdline.cpp
  core/ktest.cpp
  core/log.cpp
  core/panic.cpp
  ipc/ipc.cpp
  mm/address_space.cpp
  mm/heap.cpp
  mm/pmm.cpp
  mm/stack.cpp
  mm/vmm.cpp
  sched/sched.cpp
  smp/smp.cpp
  sync/spinlock.cpp
  sync/wait.cpp
  platform/platform.cpp
  proc/process.cpp
  proc/syscall.cpp
  proc/uaccess.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/ap_trampoline.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/apic.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/call_on_stack.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/context_switch.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/hpet.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/kernel_entry.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/gdt.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/idt.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/isr.S
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/paging.cpp
  ${CMAKE_SOURCE_DIR}/arch/x86_64/src/user.S)
target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR}/arch/x86_64/include)
target_link_libraries(kernel PRIVATE core mm acpi elf initrd cap abi)
target_link_options(kernel PRIVATE
  -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
  -Wl,-z,max-page-size=4096)
set_target_properties(kernel PROPERTIES
  OUTPUT_NAME kernel SUFFIX .elf
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
