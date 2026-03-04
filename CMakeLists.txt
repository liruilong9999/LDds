cmake_minimum_required(VERSION 3.16)

project(LDds VERSION 1.0.0 LANGUAGES C CXX)

# 全局构建选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 单配置生成器默认构建类型
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# 多配置输出后缀
set(CMAKE_RELEASE_POSTFIX "")
set(CMAKE_DEBUG_POSTFIX "d")
set(CMAKE_MINSIZEREL_POSTFIX "")
set(CMAKE_RELWITHDEBINFO_POSTFIX "")

# 编译器与平台统一行为
if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>"
    )
else()
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
        "$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
        "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
    )
endif()

# IDE 分组
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_CMakePredefinedTargets")

# 根路径变量（兼容现有 module.cmake）
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BUILD_DIR "${CMAKE_BINARY_DIR}")

message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "System : ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

include("${ROOT_DIR}/cmake/module.cmake")

add_subdirectory(src)
