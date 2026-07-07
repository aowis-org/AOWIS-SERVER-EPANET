
add_library(aowis-server-epanet-core STATIC
    
)

target_include_directories(aowis-server-epanet-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(aowis-server-epanet-core
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        epanet2
)

