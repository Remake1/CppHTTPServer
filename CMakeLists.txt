cmake_minimum_required(VERSION 3.20)
project(CppHTTPServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # For clangd / clang-tidy

option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(SANITIZE "" CACHE STRING
    "Comma-separated sanitizers to build with, e.g. 'address,undefined' or 'thread'")

# Applied to everything (including GoogleTest) so instrumented and
# uninstrumented code are never mixed, which TSan in particular requires.
if(SANITIZE)
    add_compile_options(-fsanitize=${SANITIZE} -fno-omit-frame-pointer
                        -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${SANITIZE})
endif()

function(set_project_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

# Everything except main(), so the tests can link against it.
add_library(httpcore STATIC
    src/net/TcpSocket.cpp
    src/net/TcpListener.cpp
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/handlers/StaticFileHandler.cpp
    src/HttpServer.cpp
    src/concurrency/ThreadPool.cpp
    src/event/EventLoop.cpp
    src/event/Connection.cpp
    src/EventDrivenServer.cpp
)
target_include_directories(httpcore PUBLIC ${CMAKE_SOURCE_DIR})
set_project_warnings(httpcore)

add_executable(CppHTTPServer main.cpp)
target_link_libraries(CppHTTPServer PRIVATE httpcore)
set_project_warnings(CppHTTPServer)

include(CTest)  # Defines BUILD_TESTING (ON by default)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
