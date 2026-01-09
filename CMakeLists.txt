cmake_minimum_required(VERSION 3.20)
project(CppHTTPServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CppHTTPServer
    main.cpp
    src/net/TcpSocket.cpp
    src/net/TcpListener.cpp
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/handlers/StaticFileHandler.cpp
    src/HttpServer.cpp
        src/concurrency/ThreadPool.cpp
)
target_include_directories(CppHTTPServer PRIVATE ${CMAKE_SOURCE_DIR})
