function(charmos_host_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE core)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

charmos_host_test(test_format test_format.cpp)
