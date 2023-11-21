cmake_minimum_required (VERSION 3.8)

if (NOT DEFINED RELEASE_VERSION)
    set (RELEASE_VERSION "0.0.0")
endif ()

project ("vsock-bridge" VERSION ${RELEASE_VERSION})

add_compile_options ("$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-ggdb>")
set (CXX_STANDARD 17)
enable_testing ()

set (CPACK_GENERATOR "TGZ")
set (CPACK_PACKAGE_FILE_NAME "vsock-bridge-${RELEASE_VERSION}")
include (CPack)

add_subdirectory ("vsock-bridge")
