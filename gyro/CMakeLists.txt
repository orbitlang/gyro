include(GenerateExportHeader)
include(GNUInstallDirs)

set(GYRO_PUBLIC_HEADERS
        include/gyro/allocator.h
        include/gyro/error.h
        include/gyro/gyro.h
        include/gyro/handle.h
        include/gyro/loop.h
        include/gyro/request.h)

set(GYRO_SOURCES
        src/allocator.cpp
        src/error.cpp
        src/handle.cpp
        src/gyro.cpp
        src/request.cpp
        src/platform/bsd/iopoll.cpp)

if (GYRO_BUILD_SHARED)
    add_library(gyro SHARED ${GYRO_SOURCES} ${GYRO_PUBLIC_HEADERS})
else ()
    add_library(gyro STATIC ${GYRO_SOURCES} ${GYRO_PUBLIC_HEADERS})
endif ()

add_library(gyro::gyro ALIAS gyro)

target_compile_features(gyro PUBLIC cxx_std_17)

set_target_properties(gyro PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME gyro)

# GYRO_API / GYRO_NO_EXPORT, generated into the build tree.
generate_export_header(gyro
        BASE_NAME GYRO
        EXPORT_MACRO_NAME GYRO_API
        EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/gyro/export.h")

configure_file(include/gyro/version.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/include/gyro/version.h" @ONLY)

target_include_directories(gyro
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (NOT GYRO_BUILD_SHARED)
    target_compile_definitions(gyro PUBLIC GYRO_STATIC_DEFINE)
endif ()

if (MSVC)
    target_compile_options(gyro PRIVATE /W4 /permissive- $<$<BOOL:${GYRO_WERROR}>:/WX>)

    # Nothing C++ crosses the ABI boundary.
    target_compile_options(gyro PRIVATE /EHs-c- /GR-)
    target_compile_definitions(gyro PRIVATE _HAS_EXCEPTIONS=0)
else ()
    target_compile_options(gyro PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion
            $<$<BOOL:${GYRO_WERROR}>:-Werror>)

    # Nothing C++ crosses the ABI boundary.
    target_compile_options(gyro PRIVATE -fno-exceptions -fno-rtti)
endif ()

find_package(Threads REQUIRED)
target_link_libraries(gyro PUBLIC Threads::Threads)

if (WIN32)
    target_link_libraries(gyro PRIVATE ws2_32 mswsock)
endif ()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
if (GYRO_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS gyro
            EXPORT gyroTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    install(DIRECTORY include/gyro
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h")

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/include/gyro/export.h"
            "${CMAKE_CURRENT_BINARY_DIR}/include/gyro/version.h"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gyro)

    install(EXPORT gyroTargets
            FILE gyroTargets.cmake
            NAMESPACE gyro::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gyro)

    configure_package_config_file(
            "${PROJECT_SOURCE_DIR}/cmake/gyroConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/gyroConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gyro)

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/gyroConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/gyroConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/gyroConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gyro)
endif ()
