
qt_add_executable(aowis-server-epanet
    main.cpp
    server.h
    server.cpp
)

target_link_libraries(aowis-server-epanet
    PRIVATE
        aowis-server-epanet-core
        Qt6::Core
        Qt6::Network
        Qt6::HttpServer
)
