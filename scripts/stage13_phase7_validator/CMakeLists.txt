cmake_minimum_required(VERSION 3.12)
project(stage13_phase7_validator)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED PHASE7_GEN_DIR)
    message(FATAL_ERROR "PHASE7_GEN_DIR is required")
endif()

get_filename_component(LDDS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

find_package(Qt5 REQUIRED COMPONENTS Core Network)

add_executable(stage13_phase7_validator main.cpp)

if(MSVC)
    target_compile_options(stage13_phase7_validator PRIVATE /utf-8)
endif()

target_include_directories(stage13_phase7_validator PRIVATE
    ${PHASE7_GEN_DIR}
    ${LDDS_ROOT}/src
    ${LDDS_ROOT}/src/Lib
    ${LDDS_ROOT}/src/Lib/LDdsCore
)

target_link_libraries(stage13_phase7_validator PRIVATE
    ${LDDS_ROOT}/bin/lib/LDdsCored.lib
    Qt5::Core
    Qt5::Network
)
