# Общие флаги для всех целей.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  -Wall -Wextra -Werror
  -Wshadow -Wconversion -Wsign-conversion -Wnull-dereference
  -fno-exceptions -fno-rtti)

# Цели, работающие без ОС под собой (всё, кроме host).
if(NOT CHARMOS_TARGET STREQUAL "host")
  add_compile_options(-ffreestanding -fno-stack-protector -nostdinc++ -fno-threadsafe-statics)
endif()
