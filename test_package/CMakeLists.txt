cmake_minimum_required(VERSION 3.1.2)

project(mdns_cpp_app CXX)

set(CMAKE_CXX_STANDARD 17)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(mdns_cpp REQUIRED)

add_executable(mdns_cpp_app example.cpp)
target_link_libraries(mdns_cpp_app mdns_cpp::mdns_cpp)
