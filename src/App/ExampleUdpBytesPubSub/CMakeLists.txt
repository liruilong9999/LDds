include(${ROOT_DIR}/cmake/module.cmake)

set(AbsolutePathProject ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(ProjectName ${AbsolutePathProject} NAME)

set(QT_LIBRARY_LIST "Core" "Network" "Xml")
set(SELF_LIBRARY_LIST "LDdsCore")

CreateTarget(${ProjectName} "ExeCMD")
