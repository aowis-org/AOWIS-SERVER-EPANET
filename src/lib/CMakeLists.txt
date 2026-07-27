set(AOWIS_SERVER_EPANET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(AOWIS_SERVER_EPANET_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

set(AOWIS_SERVER_EPANET_SOURCES
    epanet_resolvers.cpp
    epanet_runner.cpp
    epanet_simulation_manager.cpp
    epanet_wrapper.cpp
    dummy/dummy_networks.cpp
    utility/simulation_result_printer.cpp
    utility/simulation_status_printer.cpp
    internal/epanet_hydraulic_solver.cpp
    internal/epanet_network_builder.cpp
    internal/epanet_project.cpp
    internal/epanet_report_collector.cpp
    internal/epanet_result_reader.cpp
    internal/epanet_status_helpers.cpp
)

set(AOWIS_SERVER_EPANET_PUBLIC_HEADERS
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_api.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_resolvers.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_run_result.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_runner.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_simulation_manager.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/epanet_wrapper.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/dummy/dummy_networks.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/utility/simulation_result_printer.h
    ${AOWIS_SERVER_EPANET_INCLUDE_DIR}/aowis/epanet/utility/simulation_status_printer.h
)

set(AOWIS_SERVER_EPANET_INTERNAL_HEADERS
    internal/epanet_hydraulic_solver.h
    internal/epanet_index_registry.h
    internal/epanet_network_builder.h
    internal/epanet_project.h
    internal/epanet_report_collector.h
    internal/epanet_result_reader.h
    internal/epanet_status_helpers.h
)

add_library(aowis-server-epanet INTERFACE)
add_library(AOWIS::ServerEpanet ALIAS aowis-server-epanet)
add_library(AOWIS::Epanet ALIAS aowis-server-epanet)

add_library(aowis-server-epanet-core STATIC
    ${AOWIS_SERVER_EPANET_SOURCES}
    ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
    ${AOWIS_SERVER_EPANET_INTERNAL_HEADERS}
)

target_include_directories(aowis-server-epanet-core PUBLIC "${AOWIS_SERVER_EPANET_INCLUDE_DIR}" PRIVATE "${AOWIS_SERVER_EPANET_SOURCE_DIR}")
target_link_libraries(aowis-server-epanet-core PUBLIC Qt6::Core AOWIS::ServerModel epanet2)
target_compile_features(aowis-server-epanet-core PUBLIC cxx_std_20)
set_target_properties(aowis-server-epanet-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(aowis-server-epanet INTERFACE aowis-server-epanet-core)
