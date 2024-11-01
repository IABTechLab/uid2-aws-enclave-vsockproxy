cmake_minimum_required (VERSION 3.8)

include_directories (tests
	${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_executable (tests
		testmain.cpp
		test_buffer.cpp
		test_socket.cpp
		test_threading.cpp
)

target_link_libraries (tests vsock-io pthread)

add_test (NAME VSockTest COMMAND tests)