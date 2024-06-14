set(TARGET threadpool)

add_library(${TARGET} INTERFACE)

include(CMakePrintHelpers)
cmake_print_variables(FILESET_BASE_DIR)

target_compile_features(
    ${TARGET}
    INTERFACE
        cxx_std_20
)

target_sources(
    ${TARGET}
    INTERFACE
        FILE_SET
            HEADERS
            BASE_DIRS ${FILESET_BASE_DIR}
            FILES
                queue.hpp
                thread_pool.hpp
                timer.hpp
)

target_link_libraries(
    ${TARGET}
    INTERFACE
        pthread
)

target_include_directories(
    ${TARGET}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
)



########################################################################################################################
# Install                                                                                                              #
########################################################################################################################

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

write_basic_package_version_file(
    cppthreadpool-config-version.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)
configure_file(cppthreadpool-config.cmake.in cppthreadpool-config.cmake @ONLY)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/cppthreadpool-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cppthreadpool-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cppthreadpool
)

install(
    EXPORT cppthreadpool-targets
    FILE cppthreadpool-targets.cmake
    NAMESPACE jbo::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cppthreadpool
)

# Targets
install(
    TARGETS
        ${TARGET}
    EXPORT cppthreadpool-targets
    FILE_SET
        HEADERS
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cpp-threadpool
)
