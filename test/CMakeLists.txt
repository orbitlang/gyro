include(FetchContent)

# Use a system/vcpkg GoogleTest when available, otherwise fetch it.
find_package(GTest QUIET)

if (NOT GTest_FOUND)
    FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
            GIT_SHALLOW TRUE
            EXCLUDE_FROM_ALL)

    # Keep MSVC runtimes consistent with the consuming project.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
endif ()

include(GoogleTest)

set(GYRO_TEST_SOURCES
        gyro_test.cpp
        tcp_test.cpp
        support/minheap_test.cpp
        support/queue_test.cpp
        support/reqstore_test.cpp)

add_executable(gyro_tests ${GYRO_TEST_SOURCES})

# gmock pulls in gtest, gtest_main provides main().
target_link_libraries(gyro_tests PRIVATE gyro::gyro GTest::gmock GTest::gtest_main)

target_compile_features(gyro_tests PRIVATE cxx_std_17)

# Internal support headers are not part of the public API, but they are tested.
target_include_directories(gyro_tests PRIVATE ${PROJECT_SOURCE_DIR}/gyro/src)

if (MSVC)
    target_compile_options(gyro_tests PRIVATE /W4)
else ()
    target_compile_options(gyro_tests PRIVATE -Wall -Wextra)
endif ()

gtest_discover_tests(gyro_tests
        DISCOVERY_TIMEOUT 30
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS unit)
