set(AOWIS_SERVER_EPANET_SOURCES
    src/lib/epanet_resolvers.cpp
    src/lib/epanet_runner.cpp
    src/lib/epanet_simulation_manager.cpp
    src/lib/dummy/dummy_networks.cpp
    src/lib/dummy/random_hydraulic_network_generator.cpp
    src/lib/utility/hydraulic_simulation_result_printer.cpp
    src/lib/utility/hydraulic_simulation_status_printer.cpp
    src/lib/internal/epanet_batch_executor.cpp
    src/lib/internal/epanet_diagnostic_helpers.cpp
    src/lib/internal/epanet_hydraulic_run_configurator.cpp
    src/lib/internal/epanet_hydraulic_solver.cpp
    src/lib/internal/epanet_inp_exporter.cpp
    src/lib/internal/epanet_network_builder.cpp
    src/lib/internal/epanet_network_preparer.cpp
    src/lib/internal/epanet_network_validator.cpp
    src/lib/internal/epanet_prepared_project.cpp
    src/lib/internal/epanet_project.cpp
    src/lib/internal/epanet_project_initializer.cpp
    src/lib/internal/epanet_quality_result_reader.cpp
    src/lib/internal/epanet_quality_run_configurator.cpp
    src/lib/internal/epanet_quality_solver.cpp
    src/lib/internal/epanet_report_collector.cpp
    src/lib/internal/epanet_report_configurator.cpp
    src/lib/internal/epanet_result_finalizer.cpp
    src/lib/internal/epanet_result_reader.cpp
    src/lib/internal/epanet_single_run_executor.cpp
    src/lib/internal/epanet_status_helpers.cpp
)

set(AOWIS_SERVER_EPANET_PUBLIC_HEADERS
    include/aowis/epanet/epanet.h
    include/aowis/epanet/epanet_api.h
    include/aowis/epanet/epanet_batch_request.h
    include/aowis/epanet/epanet_resolvers.h
    include/aowis/epanet/epanet_result_batch.h
    include/aowis/epanet/epanet_result_inp.h
    include/aowis/epanet/epanet_result_run.h
    include/aowis/epanet/epanet_runner.h
    include/aowis/epanet/epanet_simulation_manager.h
    include/aowis/epanet/dummy/dummy_networks.h
    include/aowis/epanet/dummy/dummy_marburg_network_generator.h
    include/aowis/epanet/dummy/random_hydraulic_network_generator.h
    include/aowis/epanet/utility/hydraulic_simulation_result_printer.h
    include/aowis/epanet/utility/hydraulic_simulation_status_printer.h
)

set(AOWIS_SERVER_EPANET_INTERNAL_HEADERS
    src/lib/internal/epanet_batch_executor.h
    src/lib/internal/epanet_diagnostic_helpers.h
    src/lib/internal/epanet_hydraulic_run_configurator.h
    src/lib/internal/epanet_hydraulic_solver.h
    src/lib/internal/epanet_inp_exporter.h
    src/lib/internal/epanet_index_registry.h
    src/lib/internal/epanet_network_builder.h
    src/lib/internal/epanet_network_preparer.h
    src/lib/internal/epanet_network_validator.h
    src/lib/internal/epanet_prepared_project.h
    src/lib/internal/epanet_project.h
    src/lib/internal/epanet_project_initializer.h
    src/lib/internal/epanet_quality_result_reader.h
    src/lib/internal/epanet_quality_run_configurator.h
    src/lib/internal/epanet_quality_solver.h
    src/lib/internal/epanet_report_collector.h
    src/lib/internal/epanet_report_configurator.h
    src/lib/internal/epanet_result_finalizer.h
    src/lib/internal/epanet_result_reader.h
    src/lib/internal/epanet_single_run_executor.h
    src/lib/internal/epanet_status_helpers.h
)

add_library(aowis-server-epanet STATIC
    ${AOWIS_SERVER_EPANET_SOURCES}
    ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
    ${AOWIS_SERVER_EPANET_INTERNAL_HEADERS}
)

add_library(AOWIS::ServerEpanet ALIAS aowis-server-epanet)

source_group(TREE "${PROJECT_SOURCE_DIR}" FILES
    ${AOWIS_SERVER_EPANET_SOURCES}
    ${AOWIS_SERVER_EPANET_PUBLIC_HEADERS}
    ${AOWIS_SERVER_EPANET_INTERNAL_HEADERS}
)

target_include_directories(aowis-server-epanet
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/lib"
)

target_link_libraries(aowis-server-epanet
    PUBLIC
        Qt6::Core
        AOWIS::ServerModel
        epanet2
)

target_compile_features(aowis-server-epanet PUBLIC cxx_std_20)

set_target_properties(aowis-server-epanet PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
