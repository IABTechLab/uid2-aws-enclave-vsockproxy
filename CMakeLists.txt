cmake_minimum_required (VERSION 3.8)

project ("vsock-bridge")

set(CMAKE_CXX_FLAGS_DEBUG "-ggdb")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory ("vsock-bridge")
