cmake_minimum_required(VERSION 3.10)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    project(
        tiny-http-parser
        LANGUAGES C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS ON)
endif()

set(thp_src ${CMAKE_CURRENT_LIST_DIR}/tiny-http-parser.c)
add_library(thp OBJECT ${thp_src})
target_include_directories(thp PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    target_include_directories(thp PUBLIC ${CMAKE_CURRENT_LIST_DIR}/CC)
    add_executable(thp_test ${CMAKE_CURRENT_LIST_DIR}/test.c)
    target_link_libraries(thp_test PRIVATE thp)
endif()
