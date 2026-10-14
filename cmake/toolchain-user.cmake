# Программы пользователя (ring 3): статический ELF x86-64, без libc.
# SSE разрешён (состояние сохраняет ядро), red zone — по psABI.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(triple x86_64-unknown-elf)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_ASM_COMPILER_TARGET ${triple})

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(uflags "--target=${triple} -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -g")
set(CMAKE_C_FLAGS_INIT "${uflags}")
set(CMAKE_CXX_FLAGS_INIT "${uflags}")
set(CMAKE_ASM_FLAGS_INIT "--target=${triple}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--target=${triple} -nostdlib -fuse-ld=lld -static -Wl,--no-dynamic-linker")
