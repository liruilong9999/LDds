cmake_minimum_required(VERSION 3.16)
project(file2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX d)

if(NOT DEFINED LDDS_ROOT)
    if(DEFINED ENV{LDDS_ROOT})
        set(LDDS_ROOT "$ENV{LDDS_ROOT}")
    endif()
endif()
if(NOT LDDS_ROOT)
    message(FATAL_ERROR "Set LDDS_ROOT to the LDds root directory before building this module")
endif()

add_library(file2 SHARED
    file2_topic.cpp
)

target_compile_definitions(file2 PRIVATE FILE2_IDL_EXPORTS)
target_include_directories(file2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/include"
    "${LDDS_ROOT}/src/Lib/LDdsCore"
    "${CMAKE_CURRENT_SOURCE_DIR}/../file1/lib/include"
)
target_link_directories(file2 PRIVATE
    "${LDDS_ROOT}/bin/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../file1/lib/lib"
)
target_link_libraries(file2 PRIVATE LDdsCore$<$<CONFIG:Debug>:d> file1$<$<CONFIG:Debug>:d>)

set_target_properties(file2 PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/dll"
)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set_target_properties(file2 PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${CMAKE_CURRENT_SOURCE_DIR}/lib/lib"
        LIBRARY_OUTPUT_DIRECTORY_${CFG} "${CMAKE_CURRENT_SOURCE_DIR}/lib/dll"
        RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_CURRENT_SOURCE_DIR}/lib/dll"
    )
endforeach()
