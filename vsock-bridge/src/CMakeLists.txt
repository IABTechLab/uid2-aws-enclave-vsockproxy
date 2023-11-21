add_library (vsock-io "socket.cpp"  "logger.cpp" "epoll_poller.cpp")

add_executable (vsock-bridge "vsock-bridge.cpp" "config.cpp" "global.cpp")
target_link_libraries (vsock-bridge vsock-io pthread -static-libgcc -static-libstdc++)

target_include_directories (vsock-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories (vsock-bridge
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../include)

install(TARGETS vsock-bridge DESTINATION bin)
