cmake_minimum_required(VERSION 3.12)
project(stage2_tcp_reconnect_smoke)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Network)

add_executable(stage2_tcp_reconnect_smoke main.cpp)

if(MSVC)
    target_compile_options(stage2_tcp_reconnect_smoke PRIVATE /utf-8)
endif()

target_include_directories(stage2_tcp_reconnect_smoke PRIVATE
    C:/code/LDds/code/src
    C:/code/LDds/code/src/Lib
    C:/code/LDds/code/src/Lib/LDdsCore
)

target_link_libraries(stage2_tcp_reconnect_smoke PRIVATE
    C:/code/LDds/code/bin/lib/LDdsCored.lib
    Qt5::Core
    Qt5::Network
)
