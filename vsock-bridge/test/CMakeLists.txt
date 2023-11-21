add_executable (vsock-bridge-tests testmain.cpp)
target_include_directories (vsock-bridge-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (vsock-bridge-tests vsock-io pthread)

add_test (vsock-bridge-tests vsock-bridge-tests)
