
add_library(aowis-server-epanet-core STATIC
    epanet_simulation_manager.h
    epanet_simulation_manager.cpp
    
    epanet_wrapper.h
    epanet_wrapper.cpp
    
    epanet_wrapper_add.cpp
    epanet_wrapper_hydraulics.cpp
    epanet_wrapper_read.cpp
    
    model/simulation_request.h
    model/simulation_result.h
    model/epanet_status.h
    epanet_resolvers.h epanet_resolvers.cpp
    
    utility/status_printer.h
    utility/status_printer.cpp
    
    dummy/dummy_networks.h
    dummy/dummy_networks.cpp
    utility/simulation_result_printer.h utility/simulation_result_printer.cpp
)

target_include_directories(aowis-server-epanet-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(aowis-server-epanet-core
    PUBLIC
        Qt6::Core
        epanet2
)

