add_subdirectory(LIdl)
set_property(TARGET LIdl PROPERTY FOLDER "App")

add_subdirectory(LTransportTest)
set_property(TARGET LTransportTest PROPERTY FOLDER "App")

add_subdirectory(ExampleQosInit)
set_property(TARGET ExampleQosInit PROPERTY FOLDER "App/Examples")

add_subdirectory(ExampleUdpBytesPubSub)
set_property(TARGET ExampleUdpBytesPubSub PROPERTY FOLDER "App/Examples")

add_subdirectory(ExampleTypedPubSub)
set_property(TARGET ExampleTypedPubSub PROPERTY FOLDER "App/Examples")

add_subdirectory(ExampleIdlPipeline)
set_property(TARGET ExampleIdlPipeline PROPERTY FOLDER "App/Examples")



