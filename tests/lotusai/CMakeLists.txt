cmake_minimum_required(VERSION 3.16)
project(lotusai_host_tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(MAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../main)
set(CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../esp-idf/components/json/cJSON)

add_library(lotusai_cjson STATIC ${CJSON_DIR}/cJSON.c)
target_include_directories(lotusai_cjson PUBLIC ${CJSON_DIR})

add_executable(test_input_collection test_input_collection.cpp)
target_include_directories(test_input_collection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MAIN_DIR}
    ${MAIN_DIR}/boards/common
)
target_compile_definitions(test_input_collection PRIVATE CONFIG_LOTUSAI_TOP_K=3)
target_link_libraries(test_input_collection PRIVATE lotusai_cjson)

add_executable(test_selection test_selection.cpp)
target_include_directories(test_selection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CJSON_DIR})

enable_testing()
add_test(NAME input_collection COMMAND test_input_collection)
add_test(NAME selection COMMAND test_selection)
