cmake_minimum_required(VERSION 3.20)
project(cxc_os_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CXC_OS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Optional host sanitizers (freestanding kernel cannot use these).
#   cmake -S tests/host -B build/host-asan -DCXC_HOST_SANITIZER=address
#   cmake -S tests/host -B build/host-ubsan -DCXC_HOST_SANITIZER=undefined
#   cmake -S tests/host -B build/host-tsan -DCXC_HOST_SANITIZER=thread
set(CXC_HOST_SANITIZER "" CACHE STRING
    "Host-test sanitizer: empty | address | undefined | thread")
set_property(CACHE CXC_HOST_SANITIZER PROPERTY STRINGS
    "" "address" "undefined" "thread")

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

if(CXC_HOST_SANITIZER STREQUAL "address")
    target_compile_options(host_net_protocol_tests PRIVATE
        -O1 -g -fno-omit-frame-pointer -fsanitize=address)
    target_link_options(host_net_protocol_tests PRIVATE -fsanitize=address)
elseif(CXC_HOST_SANITIZER STREQUAL "undefined")
    target_compile_options(host_net_protocol_tests PRIVATE
        -O1 -g -fno-omit-frame-pointer -fsanitize=undefined
        -fno-sanitize-recover=undefined)
    target_link_options(host_net_protocol_tests PRIVATE -fsanitize=undefined)
elseif(CXC_HOST_SANITIZER STREQUAL "thread")
    target_compile_options(host_net_protocol_tests PRIVATE
        -O1 -g -fno-omit-frame-pointer -fsanitize=thread)
    target_link_options(host_net_protocol_tests PRIVATE -fsanitize=thread)
elseif(NOT CXC_HOST_SANITIZER STREQUAL "")
    message(FATAL_ERROR
        "CXC_HOST_SANITIZER must be empty, address, undefined, or thread "
        "(got '${CXC_HOST_SANITIZER}')")
endif()

enable_testing()
# High ASLR entropy breaks TSan shadow mapping on modern Linux; disable ASLR
# for the test process via setarch -R (not a product race fix).
if(CXC_HOST_SANITIZER STREQUAL "thread")
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE CXC_UNAME_M
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CXC_UNAME_RC)
    find_program(CXC_SETARCH setarch)
    if(NOT CXC_UNAME_RC EQUAL 0 OR CXC_SETARCH STREQUAL "CXC_SETARCH-NOTFOUND")
        message(FATAL_ERROR
            "thread sanitizer requires setarch and uname -m to disable ASLR")
    endif()
    add_test(NAME host_net_protocol_tests
        COMMAND "${CXC_SETARCH}" "${CXC_UNAME_M}" -R
                "$<TARGET_FILE:host_net_protocol_tests>")
else()
    add_test(NAME host_net_protocol_tests COMMAND host_net_protocol_tests)
endif()
