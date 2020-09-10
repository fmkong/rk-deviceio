cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(DeviceIo)

add_subdirectory("ThirdParty")
add_subdirectory("src")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/DeviceIO/include/DeviceIo DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
