# CMakeList.txt : CMake project for vsock-bridge, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

add_subdirectory (src)

enable_testing ()
add_subdirectory (test)