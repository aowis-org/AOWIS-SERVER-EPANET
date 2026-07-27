set(AOWIS_SERVER_EPANET_SOURCES
    src/lib/epanet_resolvers.cpp
    src/lib/epanet_runner.cpp
    src/lib/epanet_simulation_manager.cpp
    src/lib/epanet_wrapper.cpp
    src/lib/dummy/dummy_networks.cpp
    src/lib/utility/simulation_result_printer.cpp
    src/lib/utility/simulation_status_printer.cpp
    src/lib/internal/epanet_hydraulic_solver.cpp
    src/lib/internal/epanet_network_builder.cpp
    src/lib/internal/epanet_project.cpp
    src/lib/internal/epanet_report_collector.cpp
    src/lib/internal/epanet_result_reader.cpp
    src/lib/internal/epanet_status_helpers.cpp
)

set(AOWIS_SERVER_EPANET_PUBLIC_HEADERS
    include/aowis/epanet/epanet.h
    include/aowis/epanet/epanet_api.h
    include/aowis/epanet/epanet_resolvers.h
    include/aowis/epanet/epanet_run_result.h
    include/aowis/epanet/epanet_runner.h
    include/aowis/epanet/epanet_simulation_manager.h
    include/aowis/epanet/epanet_wrapper.h
    include/aowis/epanet/dummy/dummy_networks.h
    include/aowis/epanet/utility/simulation_result_printer.h
    include/aowis/epanet/utility/simulation_status_printer.h
)

set(AOWIS_SERVER_EPANET_INTERNAL_HEADERS
    src/lib/internal/epanet_hydraulic_solver.h
    src/lib/internal/epanet_index_registry.h
    src/lib/internal/epanet_network_builder.h
    src/lib/internal/epanet_project.h
    src/lib/internal/epanet_report_collector.h
    src/lib/internal/epanet_result_reader.h
    src/lib/internal/epanet_status_helpers.h
)

add_library(aowis-server-epanet INTERFACE)
add_library(AOWIS::ServerEpanet ALIAS aowis-server-epanet)
add_library(AOWIS::Epanet ALIAS aowis-server-epanet)

add_library(aowis-server-epanet-core STATIC
    ${AOWIS_SERVER_EPANET_SOURCES}
    ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
    ${AOWIS_SERVER_EPANET_INTERNAL_HEADERS}
)

source_group(
    TREE "${PROJECT_SOURCE_DIR}"
    FILES
        ${AOWIS_SERVER_EPANET_SOURCES}
        ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
        ${AOWIS_SERVER_EPANET_INTERNAL_HEADERS}
)

target_include_directories(aowis-server-epanet-core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/lib"
)

target_link_libraries(aowis-server-epanet-core
    PUBLIC
        Qt6::Core
        AOWIS::ServerModel
        epanet2
)

target_compile_features(aowis-server-epanet-core PUBLIC cxx_std_20)

set_target_properties(aowis-server-epanet-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(aowis-server-epanet
    INTERFACE
        aowis-server-epanet-core
)
