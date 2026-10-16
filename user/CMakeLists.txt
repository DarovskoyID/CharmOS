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

foreach(prog hello sys fault kread fpu iso spin cap_a cap_b cap_c chan ping pong port fs_test fs_check tcp_echo udp_test threads thread_kill thread_last spawn_parent spawn_child signals)
  charmos_user_program(${prog} tests/${prog}.cpp)
endforeach()

# Своя libc (M8): C ABI, POSIX-подмножество. Программы на ней — со своей
# точкой входа (_start из libc), без crt0 из rt.
add_library(c STATIC
  libc/src/start.cpp libc/src/string.cpp libc/src/stdlib.cpp libc/src/stdio.cpp
  libc/src/fd.cpp libc/src/pthread.cpp libc/src/socket.cpp libc/src/spawn.cpp)
target_include_directories(c PUBLIC libc/include rt/include)
target_link_libraries(c PUBLIC abi)
target_compile_options(c PRIVATE -fno-builtin)
add_subdirectory(libwm)

function(charmos_c_program name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE c)
  target_link_options(${name} PRIVATE
    -T ${CMAKE_SOURCE_DIR}/user/rt/user.ld -Wl,-z,max-page-size=4096)
  set_target_properties(${name} PROPERTIES SUFFIX .elf
    LINK_DEPENDS ${CMAKE_SOURCE_DIR}/user/rt/user.ld)
endfunction()

charmos_c_program(libc_test tests/libc_test.cpp)
charmos_c_program(init init/init.cpp)
foreach(prog logd sh echo cat ls mkdir rm sleep httpd corrupt wm wdemo)
  charmos_c_program(${prog} bin/${prog}/${prog}.cpp)
endforeach()
# Служба пакетов (M8.5) и тестовые программы для пакетов.
configure_file(bin/wm/fonts.S.in ${CMAKE_CURRENT_BINARY_DIR}/wm-fonts.S @ONLY)
target_sources(wm PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/wm-fonts.S)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/wm-fonts.S PROPERTIES OBJECT_DEPENDS
  "${CMAKE_SOURCE_DIR}/assets/fonts/space-grotesk/SpaceGrotesk-Medium.ttf;${CMAKE_SOURCE_DIR}/assets/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf")
target_link_libraries(wm PRIVATE font)
target_link_libraries(wm PRIVATE input)
target_link_libraries(wdemo PRIVATE input)
target_link_libraries(wdemo PRIVATE wm_client)
charmos_c_program(pkgd bin/pkgd/pkgd.cpp)
target_link_libraries(pkgd PRIVATE package)
foreach(app hello free)
  charmos_c_program(app_${app} tests/apps/${app}.cpp)
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
charmos_driver(virtio_net)
charmos_driver(e1000)
charmos_driver(rtl8139)
charmos_driver(xhci)
target_link_libraries(xhci PRIVATE usb)
