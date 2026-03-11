include(${ROOT_DIR}/cmake/module.cmake)

set(AbsolutePathProject ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(ProjectName ${AbsolutePathProject} NAME)

set(SELF_LIBRARY_LIST "LDdsCore;pubsub_demo")

CreateTarget(${ProjectName} "ExeCMD")

target_include_directories(${ProjectName} PRIVATE
    "${ROOT_DIR}/src/App/idl_generated"
)
