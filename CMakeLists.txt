cmake_minimum_required(VERSION 3.24)

project(
    thread_pool
    LANGUAGES
        CXX
)

set(TARGET thread-pool)
add_executable(${TARGET})

target_compile_features(
    ${TARGET}
    PUBLIC
        cxx_std_20
)

target_sources(
    ${TARGET}
    PRIVATE
        main.cpp
        thread_pool.hpp
)

target_link_libraries(
    ${TARGET}
    PRIVATE
        pthread
)
