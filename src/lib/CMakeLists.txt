set(AOWIS_SERVER_EPANET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(AOWIS_SERVER_EPANET_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

set(AOWIS_SERVER_EPANET_SOURCES
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/epanet_simulation_manager.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/epanet_wrapper.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/epanet_wrapper_add.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/epanet_wrapper_hydraulics.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/epanet_wrapper_read.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/epanet_resolvers.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/utility/simulation_status_printer.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/utility/simulation_result_printer.cpp"
    "${AOWIS_SERVER_EPANET_SOURCE_DIR}/dummy/dummy_networks.cpp"
)

set(AOWIS_SERVER_EPANET_PUBLIC_HEADERS
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_simulation_manager.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_wrapper.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_resolvers.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/utility/simulation_status_printer.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/utility/simulation_result_printer.h"
    "${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/dummy/dummy_networks.h"
)

source_group(
    TREE "${AOWIS_SERVER_EPANET_SOURCE_DIR}"
    PREFIX "src"
    FILES ${AOWIS_SERVER_EPANET_SOURCES}
)

source_group(
    TREE "${AOWIS_SERVER_EPANET_INCLUDE_DIR}"
    PREFIX "include"
    FILES ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
)

add_library(aowis-server-epanet INTERFACE)
add_library(AOWIS::ServerEpanet ALIAS aowis-server-epanet)
add_library(AOWIS::Epanet ALIAS aowis-server-epanet)

add_library(aowis-server-epanet-core STATIC
    ${AOWIS_SERVER_EPANET_SOURCES}
    ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
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
