add_executable(aowis-server-epanet-result-contract-tests
    test_result_contract.cpp
)

target_link_libraries(aowis-server-epanet-result-contract-tests
    PRIVATE
        AOWIS::ServerEpanet
)

target_compile_features(aowis-server-epanet-result-contract-tests PRIVATE cxx_std_20)

add_test(
    NAME aowis-server-epanet-result-contract
    COMMAND aowis-server-epanet-result-contract-tests
)
