cmake_minimum_required(VERSION 3.25)

# Суперсборка: каждая цель (uefi, host, позже kernel/user) собирается
# отдельным подпроектом со своим toolchain-файлом.
# Подпроект узнаётся по CHARMOS_TARGET.

if(NOT DEFINED CHARMOS_TARGET)
  project(CharmOS NONE)
  include(ExternalProject)

  function(charmos_subbuild name)
    ExternalProject_Add(${name}
      SOURCE_DIR ${CMAKE_SOURCE_DIR}
      BINARY_DIR ${CMAKE_BINARY_DIR}/${name}
      CMAKE_GENERATOR Ninja
      CMAKE_ARGS -DCHARMOS_TARGET=${name} ${ARGN}
                 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
      INSTALL_COMMAND ""
      BUILD_ALWAYS ON
      USES_TERMINAL_BUILD ON)
  endfunction()

  charmos_subbuild(uefi
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchain-uefi.cmake)
  charmos_subbuild(host -DCMAKE_BUILD_TYPE=Debug)
  return()
endif()

project(CharmOS-${CHARMOS_TARGET} LANGUAGES C CXX ASM)
include(${CMAKE_SOURCE_DIR}/cmake/flags.cmake)

add_library(abi INTERFACE)
target_include_directories(abi INTERFACE ${CMAKE_SOURCE_DIR}/include)

add_subdirectory(lib/core)

if(CHARMOS_TARGET STREQUAL "uefi")
  add_subdirectory(boot/uefi)
elseif(CHARMOS_TARGET STREQUAL "host")
  enable_testing()
  add_subdirectory(tests/host)
else()
  message(FATAL_ERROR "Unknown CHARMOS_TARGET: ${CHARMOS_TARGET}")
endif()
