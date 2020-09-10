cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)

PROJECT (deviceio_netlink_test)

message(${PROJECT_SOURCE_DIR})
message(${PROJECT_BINARY_DIR})

add_definitions("-Wall -g")

set(SRC_FILES
    NetlinkTest.cpp
)

add_executable(deviceio_netlink_test ${SRC_FILES})
target_link_libraries(deviceio_netlink_test pthread DeviceIo asound)

install(TARGETS deviceio_netlink_test DESTINATION bin)
