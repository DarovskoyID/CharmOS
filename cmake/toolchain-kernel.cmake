# Ядро: ELF x86-64, higher half. Подключается в M2.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(triple x86_64-unknown-elf)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_ASM_COMPILER_TARGET ${triple})
set(CMAKE_ASM_FLAGS_INIT "--target=${triple}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(kflags "--target=${triple} -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -g")
set(CMAKE_C_FLAGS_INIT "${kflags}")
set(CMAKE_CXX_FLAGS_INIT "${kflags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--target=${triple} -nostdlib -fuse-ld=lld -static -Wl,--no-dynamic-linker")
