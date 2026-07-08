
add_library(aowis-server-epanet-core STATIC
    epanet_wrapper.h
    epanet_wrapper.cpp
    _enums_structs.h
    
    
    
    
    
    
    
    
    
    
    
    
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

