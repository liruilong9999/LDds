add_subdirectory(LIdl)
set_property(TARGET LIdl PROPERTY FOLDER "App")

add_subdirectory(idl_generated)
set_property(TARGET pubsub_demo PROPERTY FOLDER "App/Generated")

add_subdirectory(pub)
set_property(TARGET pub PROPERTY FOLDER "App/Examples")

add_subdirectory(sub)
set_property(TARGET sub PROPERTY FOLDER "App/Examples")



