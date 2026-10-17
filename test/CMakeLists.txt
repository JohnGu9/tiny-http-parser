cmake_minimum_required(VERSION 3.10)

add_library(common STATIC ${CMAKE_CURRENT_LIST_DIR}/common.c)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(common PUBLIC thp)

add_executable(multi_part_test ${CMAKE_CURRENT_LIST_DIR}/multi_part.c)
target_link_libraries(multi_part_test PRIVATE thp common)

add_executable(chunk_test ${CMAKE_CURRENT_LIST_DIR}/chunk.c)
target_link_libraries(chunk_test PRIVATE thp common)

add_executable(body_test ${CMAKE_CURRENT_LIST_DIR}/body.c)
target_link_libraries(body_test PRIVATE thp common)

add_executable(how_to_handle_error ${CMAKE_CURRENT_LIST_DIR}/how-to-handle-error.c)
target_link_libraries(how_to_handle_error PRIVATE thp common)
