set(TARGET threadpool-example-timers-threadpool)

add_executable(${TARGET})

target_link_libraries(
    ${TARGET}
    PRIVATE
        threadpool
)

target_sources(
    ${TARGET}
    PRIVATE
        main.cpp
)
