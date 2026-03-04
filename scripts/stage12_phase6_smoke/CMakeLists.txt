cmake_minimum_required(VERSION 3.16)
project(stage12_phase6_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(LDDS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(DEFINED ENV{QT_DIR} AND NOT "$ENV{QT_DIR}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}/lib/cmake")
endif()

find_package(Qt6 QUIET COMPONENTS Core Network)
if(Qt6_FOUND)
    set(LDDS_QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Network)
    set(LDDS_QT_PACKAGE Qt5)
endif()

add_executable(stage12_phase6_smoke main.cpp)

if(MSVC)
    target_compile_options(stage12_phase6_smoke PRIVATE /utf-8)
endif()

target_include_directories(stage12_phase6_smoke PRIVATE
    "${LDDS_ROOT}/src"
    "${LDDS_ROOT}/src/Lib"
    "${LDDS_ROOT}/src/Lib/LDdsCore"
)

set(LDDS_IMPORTED_TARGET "${PROJECT_NAME}_LDdsCoreImported")
add_library(${LDDS_IMPORTED_TARGET} SHARED IMPORTED)
if(WIN32)
    set_target_properties(${LDDS_IMPORTED_TARGET} PROPERTIES
        IMPORTED_IMPLIB_DEBUG "${LDDS_ROOT}/bin/lib/LDdsCored.lib"
        IMPORTED_IMPLIB_RELEASE "${LDDS_ROOT}/bin/lib/LDdsCore.lib"
        IMPORTED_IMPLIB_RELWITHDEBINFO "${LDDS_ROOT}/bin/lib/LDdsCore.lib"
        IMPORTED_IMPLIB_MINSIZEREL "${LDDS_ROOT}/bin/lib/LDdsCore.lib"
        IMPORTED_LOCATION_DEBUG "${LDDS_ROOT}/bin/LDdsCored.dll"
        IMPORTED_LOCATION_RELEASE "${LDDS_ROOT}/bin/LDdsCore.dll"
        IMPORTED_LOCATION_RELWITHDEBINFO "${LDDS_ROOT}/bin/LDdsCore.dll"
        IMPORTED_LOCATION_MINSIZEREL "${LDDS_ROOT}/bin/LDdsCore.dll"
    )
elseif(APPLE)
    set_target_properties(${LDDS_IMPORTED_TARGET} PROPERTIES
        IMPORTED_LOCATION_DEBUG "${LDDS_ROOT}/bin/lib/libLDdsCored.dylib"
        IMPORTED_LOCATION_RELEASE "${LDDS_ROOT}/bin/lib/libLDdsCore.dylib"
        IMPORTED_LOCATION_RELWITHDEBINFO "${LDDS_ROOT}/bin/lib/libLDdsCore.dylib"
        IMPORTED_LOCATION_MINSIZEREL "${LDDS_ROOT}/bin/lib/libLDdsCore.dylib"
    )
else()
    set_target_properties(${LDDS_IMPORTED_TARGET} PROPERTIES
        IMPORTED_LOCATION_DEBUG "${LDDS_ROOT}/bin/lib/libLDdsCored.so"
        IMPORTED_LOCATION_RELEASE "${LDDS_ROOT}/bin/lib/libLDdsCore.so"
        IMPORTED_LOCATION_RELWITHDEBINFO "${LDDS_ROOT}/bin/lib/libLDdsCore.so"
        IMPORTED_LOCATION_MINSIZEREL "${LDDS_ROOT}/bin/lib/libLDdsCore.so"
    )
endif()

target_link_libraries(stage12_phase6_smoke PRIVATE
    ${LDDS_IMPORTED_TARGET}
    ${LDDS_QT_PACKAGE}::Core
    ${LDDS_QT_PACKAGE}::Network
)
