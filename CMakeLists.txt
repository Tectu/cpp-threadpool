cmake_minimum_required(VERSION 3.25)

project(
    cpp-threadpool
    VERSION 0.0.0
    LANGUAGES
        CXX
)

option(THREADPOOL_BUILD_EXAMPLES "Whether to build examples" OFF)

add_subdirectory(lib)

if (THREADPOOL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
