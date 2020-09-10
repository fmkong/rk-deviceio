cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)

PROJECT (rkmediaplayer)

file(GLOB_RECURSE rkmediaplayer_SRC_C "${rkmediaplayer_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE rkmediaplayer_SRC_CXX "${rkmediaplayer_SOURCE_DIR}/src/*.cpp")

file(GLOB_RECURSE rkmediaplayer_INC "${rkmediaplayer_SOURCE_DIR}/include/DeviceIo/*.h")

add_library(rkmediaplayer SHARED
		${rkmediaplayer_SRC_C}
		${rkmediaplayer_SRC_CXX}
		)

find_package(PkgConfig)
pkg_check_modules (GSTREAMER gstreamer-1.0 REQUIRED)
include_directories(${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(rkmediaplayer ${GSTREAMER_LIBRARIES})

target_include_directories(rkmediaplayer PUBLIC "include/DeviceIo")

# install target
install(FILES ${rkmediaplayer_INC} DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
install(FILES ${rkmediaplayer_INC} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/DeviceIo)
install(TARGETS rkmediaplayer DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
