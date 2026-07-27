set(AOWIS_SERVER_EPANET_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")

add_library(aowis-server-epanet INTERFACE)
add_library(AOWIS::ServerEpanet ALIAS aowis-server-epanet)
add_library(AOWIS::Epanet ALIAS aowis-server-epanet)

add_library(aowis-server-epanet-core STATIC
    epanet_simulation_manager.cpp
    epanet_wrapper.cpp
    epanet_wrapper_add.cpp
    epanet_wrapper_hydraulics.cpp
    epanet_wrapper_read.cpp
    epanet_resolvers.cpp
    utility/simulation_status_printer.cpp
    utility/simulation_result_printer.cpp
    dummy/dummy_networks.cpp

    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_simulation_manager.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_wrapper.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_resolvers.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/utility/simulation_status_printer.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/utility/simulation_result_printer.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/dummy/dummy_networks.h"
)

target_include_directories(aowis-server-epanet-core
    PUBLIC
        "${AOWIS_SERVER_EPANET_INCLUDE_DIR}"
)

target_link_libraries(aowis-server-epanet-core
    PUBLIC
        Qt6::Core
        AOWIS::ServerModel
        epanet2
)

target_compile_features(aowis-server-epanet-core
    PUBLIC
        cxx_std_20
)

target_link_libraries(aowis-server-epanet
    INTERFACE
        aowis-server-epanet-core
)
