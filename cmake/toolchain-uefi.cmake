# UEFI-приложение: PE32+, MS x64 ABI, без CRT.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(triple x86_64-unknown-windows)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_ASM_COMPILER_TARGET ${triple})

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "--target=${triple} -mno-red-zone -fshort-wchar")
set(CMAKE_CXX_FLAGS_INIT "--target=${triple} -mno-red-zone -fshort-wchar")
set(CMAKE_EXE_LINKER_FLAGS_INIT
  "--target=${triple} -nostdlib -fuse-ld=lld -Wl,-subsystem:efi_application -Wl,-entry:efi_main")
