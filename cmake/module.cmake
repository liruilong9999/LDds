# 定义宏 `CreateTarget`，保持现有工程结构与调用方式不变。
# ARGV0: ProjectName
# ARGV1: Type（Exe / ExeCMD / Lib / Dll）
# ARGV2: 可选输出子目录
macro(CreateTarget ProjectName Type)
    if(NOT("${QT_LIBRARY_LIST}" STREQUAL ""))
        include("${ROOT_DIR}/cmake/module_qt.cmake")
    endif()

    message(STATUS "${ProjectName}")
    project(${ProjectName} LANGUAGES CXX)

    set(CURRENT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
    set(HEADER_FILES "")
    set(SOURCE_FILES "")
    set(FORM_FILES "")
    set(RESOURCE_FILES "")

    file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
        "${CURRENT_PATH}/*.h" "${CURRENT_PATH}/*.hpp")
    file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
        "${CURRENT_PATH}/*.c" "${CURRENT_PATH}/*.cpp")
    file(GLOB_RECURSE FORM_FILES CONFIGURE_DEPENDS
        "${CURRENT_PATH}/*.ui")
    file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS
        "${CURRENT_PATH}/*.qrc")

    if(WIN32)
        source_group(TREE "${CURRENT_PATH}" PREFIX "Header Files" FILES ${HEADER_FILES})
        source_group(TREE "${CURRENT_PATH}" PREFIX "Source Files" FILES ${SOURCE_FILES})
        source_group(TREE "${CURRENT_PATH}" PREFIX "Form Files" FILES ${FORM_FILES})
        source_group(TREE "${CURRENT_PATH}" PREFIX "Resource Files" FILES ${RESOURCE_FILES})
    endif()

    set(CONFIGURATION_TYPES "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
    if(${ARGC} GREATER 2)
        foreach(CONFIGURATION_TYPE ${CONFIGURATION_TYPES})
            string(TOUPPER ${CONFIGURATION_TYPE} CFG)
            set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin/${ARGV2}")
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin/${ARGV2}")
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin/${ARGV2}")
        endforeach()
    else()
        foreach(CONFIGURATION_TYPE ${CONFIGURATION_TYPES})
            string(TOUPPER ${CONFIGURATION_TYPE} CFG)
            set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin/lib")
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin/lib")
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin")
        endforeach()
    endif()

    set(CMAKE_RELEASE_POSTFIX "")
    set(CMAKE_DEBUG_POSTFIX "d")
    set(CMAKE_MINSIZEREL_POSTFIX "")
    set(CMAKE_RELWITHDEBINFO_POSTFIX "")

    if(${Type} STREQUAL "Exe")
        if(WIN32)
            add_executable(${PROJECT_NAME} WIN32
                ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
        else()
            add_executable(${PROJECT_NAME}
                ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
        endif()
    elseif(${Type} STREQUAL "ExeCMD")
        add_executable(${PROJECT_NAME}
            ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
    elseif(${Type} STREQUAL "Lib")
        add_library(${PROJECT_NAME} STATIC
            ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
    elseif(${Type} STREQUAL "Dll")
        add_library(${PROJECT_NAME} SHARED
            ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
    else()
        message(FATAL_ERROR "Unsupported target type: ${Type}")
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE
        "${ROOT_DIR}/src"
        "${ROOT_DIR}/src/Lib"
        "${ROOT_DIR}/src/Lib/LDdsCore"
        "${CURRENT_PATH}/../"
    )

    if(MSVC)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
    endif()

    if(NOT("${QT_LIBRARY_LIST}" STREQUAL ""))
        AddQtInc("${QT_LIBRARY_LIST}")
        AddQtLib("${QT_LIBRARY_LIST}")
    endif()

    foreach(_lib ${SELF_LIBRARY_LIST})
        if(TARGET ${_lib})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_lib})
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_lib}$<$<CONFIG:Debug>:d>)
        endif()
    endforeach()

    if(WIN32)
        source_group("CMake Rules"  REGULAR_EXPRESSION "^$")
        source_group("Header Files" REGULAR_EXPRESSION "^$")
        source_group("Source Files" REGULAR_EXPRESSION "^$")
        source_group("zero"         REGULAR_EXPRESSION "\\.h$|\\.cpp$|\\.stamp$|\\.rule$")
    endif()
endmacro()
