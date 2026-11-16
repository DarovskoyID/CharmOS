add_library(core STATIC src/format.cpp src/fmt.cpp src/math.cpp)
target_include_directories(core PUBLIC include)

# Хост берёт mem* из системной libc; во freestanding-целях они наши,
# компилятор вправе генерировать вызовы к ним сам.
if(NOT CHARMOS_TARGET STREQUAL "host")
  target_sources(core PRIVATE src/mem.cpp src/cxxrt.cpp)
  # Large native ELF/ICU copies hit these primitives even in debug builds.
  # Keep diagnostics elsewhere; do not let loop idioms call libc recursively.
  set_source_files_properties(src/mem.cpp PROPERTIES COMPILE_OPTIONS "-O2;-fno-builtin")
endif()
if(CHARMOS_TARGET STREQUAL "kernel")
  target_sources(core PRIVATE ${CMAKE_SOURCE_DIR}/arch/x86_64/src/memory.S)
  target_compile_definitions(core PRIVATE CHARMOS_KERNEL_MEMORY)
endif()
