cmake_minimum_required (VERSION 3.8)

add_library (vsock-io "socket.cpp"  "logger.cpp" "epoll_poller.cpp")

add_executable (vsock-bridge "vsock-bridge.cpp" "config.cpp" "global.cpp")
target_link_libraries(vsock-bridge vsock-io pthread)

target_include_directories(vsock-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories(vsock-bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

set_property(TARGET vsock-io PROPERTY CXX_STANDARD 17)
set_property(TARGET vsock-bridge PROPERTY CXX_STANDARD 17)