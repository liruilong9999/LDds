# 仅在使用 Qt 的目标中引入本模块。

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

macro(_ldds_prepare_qt)
    if(NOT DEFINED LDDS_QT_PACKAGE)
        if(DEFINED ENV{QT_DIR} AND NOT "$ENV{QT_DIR}" STREQUAL "")
            list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
            list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}/lib/cmake")
        endif()

        find_package(Qt6 QUIET COMPONENTS Core)
        if(Qt6_FOUND)
            set(LDDS_QT_PACKAGE Qt6 CACHE INTERNAL "Qt major version used by LDds")
        else()
            find_package(Qt5 REQUIRED COMPONENTS Core)
            set(LDDS_QT_PACKAGE Qt5 CACHE INTERNAL "Qt major version used by LDds")
        endif()

        message(STATUS "LDds Qt package: ${LDDS_QT_PACKAGE}")
    endif()
endmacro()

# ARGV0: Qt 组件列表（如 "Core;Network;Xml"）
macro(AddQtInc QtLibraryList)
    _ldds_prepare_qt()
    foreach(qt_library ${QtLibraryList})
        find_package(${LDDS_QT_PACKAGE} REQUIRED COMPONENTS ${qt_library})
    endforeach()
endmacro()

macro(AddQtLib QtLibraryList)
    _ldds_prepare_qt()
    foreach(qt_library ${QtLibraryList})
        find_package(${LDDS_QT_PACKAGE} REQUIRED COMPONENTS ${qt_library})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LDDS_QT_PACKAGE}::${qt_library})
    endforeach()
endmacro()
