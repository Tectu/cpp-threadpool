set(TARGET threadpool-example-timers-modify_interval)

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
