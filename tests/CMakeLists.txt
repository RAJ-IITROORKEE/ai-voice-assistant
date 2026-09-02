cmake_minimum_required(VERSION 3.16)
project(luna_host_tests LANGUAGES CXX)

enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(add_luna_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../main/include")
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_luna_test(relay_protocol_test relay_protocol_test.cpp "${CMAKE_CURRENT_LIST_DIR}/../main/relay_protocol.cpp")
add_luna_test(playback_policy_test playback_policy_test.cpp "${CMAKE_CURRENT_LIST_DIR}/../main/playback_policy.cpp")
add_luna_test(relay_uplink_policy_test relay_uplink_policy_test.cpp)
