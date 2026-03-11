add_library(pubsub_demo SHARED
    pubsub_demo_topic.cpp
)

target_compile_definitions(pubsub_demo PRIVATE PUBSUB_DEMO_IDL_EXPORTS)

target_include_directories(pubsub_demo PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ROOT_DIR}/src/Lib/LDdsCore"
)

target_link_libraries(pubsub_demo PRIVATE LDdsCore)

foreach(CONFIGURATION_TYPE Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER ${CONFIGURATION_TYPE} CFG)
    set_target_properties(pubsub_demo PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin/lib"
        LIBRARY_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_${CFG} "${ROOT_DIR}/bin"
    )
endforeach()

set_target_properties(pubsub_demo PROPERTIES
    DEBUG_POSTFIX "d"
)
