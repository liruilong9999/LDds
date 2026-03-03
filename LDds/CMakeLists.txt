# LDds - Lightweight DDS Framework
# CMake build configuration

cmake_minimum_required(VERSION 3.14)
project(LDds VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for MSVC
if(MSVC)
    # /W4 - warning level 4
    # /WX- - warnings are not errors
    # /utf-8 - use UTF-8 encoding for source files
    add_compile_options(/W4 /WX- /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files for LDds library
set(LDDS_CORE_SOURCES
    src/core/LDomain.cpp
    src/core/LFindSet.cpp
    src/core/LQos.cpp
)

set(LDDS_IDL_SOURCES
    src/idl/LIdlParser.cpp
    src/idl/LIdlGenerator.cpp
)

# Create static library target
add_library(LDds STATIC
    ${LDDS_CORE_SOURCES}
    ${LDDS_IDL_SOURCES}
)

# Public include directories for consumers
target_include_directories(LDds
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# lidl executable target
add_executable(lidl
    src/idl/main.cpp
)

# Link lidl against LDds library
target_link_libraries(lidl PRIVATE LDds)

# Installation rules (optional)
install(TARGETS LDds lidl
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
