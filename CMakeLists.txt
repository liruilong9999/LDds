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

if(MSVC)
    set(PWSH_COMPAT_DIR "${CMAKE_BINARY_DIR}/codex_tools")
    file(MAKE_DIRECTORY "${PWSH_COMPAT_DIR}")
    set(PWSH_COMPAT_EXE "")

    if(DEFINED ENV{WINDIR})
        set(_powershell_exe "$ENV{WINDIR}/System32/WindowsPowerShell/v1.0/powershell.exe")
        if(EXISTS "${_powershell_exe}" AND NOT EXISTS "${PWSH_COMPAT_DIR}/pwsh.exe")
            file(COPY "${_powershell_exe}" DESTINATION "${PWSH_COMPAT_DIR}")
            if(EXISTS "${PWSH_COMPAT_DIR}/powershell.exe")
                file(RENAME "${PWSH_COMPAT_DIR}/powershell.exe" "${PWSH_COMPAT_DIR}/pwsh.exe")
            endif()
        endif()
        if(EXISTS "${PWSH_COMPAT_DIR}/pwsh.exe")
            set(PWSH_COMPAT_EXE "${PWSH_COMPAT_DIR}/pwsh.exe")
        endif()
    endif()
endif()

message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "System : ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

include("${ROOT_DIR}/cmake/module.cmake")

add_subdirectory(src)


install(DIRECTORY "${ROOT_DIR}/src/Lib/" DESTINATION "${ROOT_DIR}/bin/include"
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# 安装到目标目录，为了作为库
install(DIRECTORY "${ROOT_DIR}/bin/include" DESTINATION "${ROOT_DIR}/z_dds_install")

file(GLOB LIB_FILES "${ROOT_DIR}/bin/lib/*.lib") 
install(FILES ${LIB_FILES} DESTINATION "${ROOT_DIR}/z_dds_install/lib")

file(GLOB DLL_FILES "${ROOT_DIR}/bin/*.dll") 
install(FILES ${DLL_FILES} DESTINATION "${ROOT_DIR}/z_dds_install")

file(GLOB DLL_FILES "${ROOT_DIR}/bin/Lidl.exe") 
install(FILES ${DLL_FILES} DESTINATION "${ROOT_DIR}/z_dds_install")
