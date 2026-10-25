cmake_minimum_required(VERSION 3.20)
project(cxc_os_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CXC_OS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(host_net_protocol_tests
    net_protocol_tests.cpp
    scheduler_event_stub.cpp
    "${CXC_OS_ROOT}/src/net/checksum.cpp"
    "${CXC_OS_ROOT}/src/net/datagram.cpp"
    "${CXC_OS_ROOT}/src/net/proto.cpp"
    "${CXC_OS_ROOT}/src/net/socket.cpp"
)
target_include_directories(host_net_protocol_tests PRIVATE
    "${CXC_OS_ROOT}/include"
)
target_compile_options(host_net_protocol_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

enable_testing()
add_test(NAME host_net_protocol_tests COMMAND host_net_protocol_tests)
