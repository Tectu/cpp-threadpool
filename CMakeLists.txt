cmake_minimum_required(VERSION 3.23)

project(
    cpp-threadpool
    LANGUAGES
        CXX
)

option(THREADPOOL_BUILD_EXAMPLES "Whether to build examples" ON)

add_subdirectory(lib)

if (THREADPOOL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
