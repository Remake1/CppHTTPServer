include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---- Unit tests: components in isolation ----
add_executable(unit_tests
    unit/HttpRequestTest.cpp
    unit/HttpResponseTest.cpp
    unit/StaticFileHandlerTest.cpp
    unit/ConnectionTest.cpp
    unit/EventLoopTest.cpp
)
target_link_libraries(unit_tests PRIVATE httpcore GTest::gtest_main)
set_project_warnings(unit_tests)

include(GoogleTest)
gtest_discover_tests(unit_tests DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 60)

# ---- Integration tests: the real binary over real sockets ----
# One CTest entry per test class (each starts its own server on a free port),
# so `ctest -j` runs them in parallel.
find_package(Python3 3.8 COMPONENTS Interpreter REQUIRED)

set(COMMON_CLASSES FileServing KeepAlive Robustness)
set(EVENT_ONLY_CLASSES Pipelining ErrorHandling Timeouts FdExhaustion)

function(add_integration_test mode class)
    add_test(
        NAME integration.${mode}.${class}
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_server.py
                --binary $<TARGET_FILE:CppHTTPServer> --mode ${mode} ${class}
    )
    set_tests_properties(integration.${mode}.${class} PROPERTIES TIMEOUT 120)
endfunction()

foreach(class IN LISTS COMMON_CLASSES EVENT_ONLY_CLASSES)
    add_integration_test(event ${class})
endforeach()
foreach(class IN LISTS COMMON_CLASSES)
    add_integration_test(threaded ${class})
endforeach()
