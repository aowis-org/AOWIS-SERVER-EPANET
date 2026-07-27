qt_add_executable(aowis-server-epanet-server
    main.cpp
    server.h
    server.cpp
)

set_target_properties(aowis-server-epanet-server PROPERTIES
    OUTPUT_NAME "aowis-server-epanet"
)

target_link_libraries(aowis-server-epanet-server
    PRIVATE
        aowis-server-epanet-core
        Qt6::Core
        Qt6::Network
        Qt6::HttpServer
)
