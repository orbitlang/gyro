cmake_minimum_required(VERSION 3.20)

project(gyro
        VERSION 0.1.0
        DESCRIPTION "Cross-platform asynchronous event loop and I/O runtime"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(GYRO_BUILD_TESTS "Build the gyro test suite" ${PROJECT_IS_TOP_LEVEL})
option(GYRO_BUILD_SHARED "Build gyro as a shared library" OFF)
option(GYRO_INSTALL "Generate the gyro install target" ${PROJECT_IS_TOP_LEVEL})
option(GYRO_WERROR "Treat compiler warnings as errors" OFF)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    # Hide symbols by default: only what gyro explicitly exports is visible.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

    # Keep all artifacts in one place regardless of generator.
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
    endif ()
endif ()

add_subdirectory(gyro)

if (GYRO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif ()
