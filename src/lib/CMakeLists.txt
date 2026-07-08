
add_library(aowis-server-epanet-core STATIC
    hydraulic_simulation_engine.h
    hydraulic_simulation_engine.cpp
    simulation_request.h
    simulation_request.cpp
    simulation_result.h
    simulation_result.cpp
    simulation_json.h
    simulation_json.cpp
    epanet_error.h
    epanet_error.cpp
    epanet_project.h
    epanet_project.cpp
    epanet_simulation_engine.h
    epanet_simulation_engine.cpp
)

target_include_directories(aowis-server-epanet-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(aowis-server-epanet-core
    PUBLIC
        Qt6::Core
    PRIVATE
        epanet2
)

