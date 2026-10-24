cmake_minimum_required(VERSION 3.10)
project(UltimateASILoaderAndroid LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ASILoader SHARED
    ASILoader.cpp
)

target_include_directories(ASILoader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ASILoader PRIVATE
    log
    dl
)

set_target_properties(ASILoader PROPERTIES
    OUTPUT_NAME "ASILoader"
    PREFIX "lib"
    SUFFIX ".so"
)
