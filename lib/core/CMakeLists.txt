add_library(core STATIC src/format.cpp src/fmt.cpp src/math.cpp)
target_include_directories(core PUBLIC include)

# Хост берёт mem* из системной libc; во freestanding-целях они наши,
# компилятор вправе генерировать вызовы к ним сам.
if(NOT CHARMOS_TARGET STREQUAL "host")
  target_sources(core PRIVATE src/mem.cpp)
endif()
