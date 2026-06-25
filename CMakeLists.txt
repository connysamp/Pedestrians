cmake_minimum_required(VERSION 3.15)
project(pedestrians CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(pedestrians SHARED src/main.cpp sdk/amxplugin.cpp)

target_include_directories(pedestrians PRIVATE sdk src)

# For SA:MP plugins on Windows, we need to output a .dll
if(WIN32)
    set_target_properties(pedestrians PROPERTIES PREFIX "" SUFFIX ".dll")
    # Def file for exporting the SA:MP plugin functions
    target_link_options(pedestrians PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/pedestrians.def)
elseif(UNIX)
    # SA:MP Linux servers are 32-bit
    add_compile_definitions(LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
    set_target_properties(pedestrians PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif()
