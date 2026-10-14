add_library(rt STATIC rt/crt0.S)
target_include_directories(rt PUBLIC rt/include)
target_link_libraries(rt PUBLIC abi)

function(charmos_user_program name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE rt core)
  target_link_options(${name} PRIVATE
    -T ${CMAKE_SOURCE_DIR}/user/rt/user.ld -Wl,-z,max-page-size=4096)
  set_target_properties(${name} PROPERTIES SUFFIX .elf
    LINK_DEPENDS ${CMAKE_SOURCE_DIR}/user/rt/user.ld)
endfunction()

foreach(prog hello sys fault kread fpu iso spin cap_a cap_b cap_c chan ping pong port fs_test fs_check)
  charmos_user_program(${prog} tests/${prog}.cpp)
endforeach()

# Драйверы: drivers/<имя>.elf и drivers/<имя>.toml (docs/abi/driver.md §7).
function(charmos_driver name)
  charmos_user_program(${name} drivers/${name}/${name}.cpp)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/drivers)
  add_custom_command(TARGET ${name} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/user/drivers/${name}/${name}.toml ${CMAKE_BINARY_DIR}/drivers/${name}.toml)
endfunction()

charmos_driver(edu)
charmos_driver(virtio_blk)
charmos_driver(nvme)
charmos_driver(ahci)
