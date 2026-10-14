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

foreach(prog hello sys fault kread fpu iso spin cap_a cap_b cap_c chan ping pong)
  charmos_user_program(${prog} tests/${prog}.cpp)
endforeach()
