set(TARGET threadpool)

add_library(${TARGET} INTERFACE IMPORTED GLOBAL)

target_compile_features(
    ${TARGET}
    INTERFACE
        cxx_std_20
)

target_sources(
    ${TARGET}
    INTERFACE
        thread_pool.hpp
)

target_link_libraries(
    ${TARGET}
    INTERFACE
        pthread
)

target_include_directories(
    ${TARGET}
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
