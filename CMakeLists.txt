cmake_minimum_required(VERSION 3.10)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    project(tiny-http-parser)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS ON)

    if(MSVC)
        add_compile_options(/Wall)
    else()
        add_compile_options(-Wall)
    endif()
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/kmp)

set(thp_src ${CMAKE_CURRENT_LIST_DIR}/tiny-http-parser.c)
add_library(thp STATIC ${thp_src})
target_include_directories(thp PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(thp PRIVATE kmp)

if(MSVC)
else()
    target_compile_options(thp PRIVATE -Wextra -Wshadow)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test)
    add_executable(thp_test ${CMAKE_CURRENT_LIST_DIR}/test.c)
    target_link_libraries(thp_test PRIVATE thp common)
endif()
