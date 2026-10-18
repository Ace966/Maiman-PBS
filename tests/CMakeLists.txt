add_executable(CommunicationFoundationTests tst_communicationfoundation.cpp)
set_target_properties(CommunicationFoundationTests PROPERTIES AUTOMOC ON)
target_link_libraries(CommunicationFoundationTests
    PRIVATE MaimanPBS::Communication Qt6::Test
)

add_executable(SerialDataSourceTests tst_serialdatasource.cpp)
set_target_properties(SerialDataSourceTests PROPERTIES AUTOMOC ON)
target_link_libraries(SerialDataSourceTests
    PRIVATE MaimanPBS::Communication Qt6::Test
)

add_test(NAME communication_foundation
    COMMAND CommunicationFoundationTests -o -,txt
        -o "${CMAKE_CURRENT_BINARY_DIR}/communication-foundation.txt,txt"
)
add_test(NAME serial_data_source
    COMMAND SerialDataSourceTests -o -,txt
        -o "${CMAKE_CURRENT_BINARY_DIR}/serial-data-source.txt,txt"
)
add_executable(DataThreadTests tst_datathread.cpp)
set_target_properties(DataThreadTests PROPERTIES AUTOMOC ON)
target_link_libraries(DataThreadTests PRIVATE MaimanPBS::Communication Qt6::Test)
add_test(NAME data_thread COMMAND DataThreadTests -o -,txt
    -o "${CMAKE_CURRENT_BINARY_DIR}/data-thread.txt,txt")
add_executable(PICTextProtocolTests tst_pictextprotocol.cpp)
set_target_properties(PICTextProtocolTests PROPERTIES AUTOMOC ON)
target_link_libraries(PICTextProtocolTests PRIVATE MaimanPBS::Communication Qt6::Test)
add_test(NAME pic_text_protocol COMMAND PICTextProtocolTests -o -,txt
    -o "${CMAKE_CURRENT_BINARY_DIR}/pic-text-protocol.txt,txt")
add_executable(PICTextReceiverTests tst_pictextreceiver.cpp)
set_target_properties(PICTextReceiverTests PROPERTIES AUTOMOC ON)
target_link_libraries(PICTextReceiverTests PRIVATE MaimanPBS::Communication Qt6::Test)
add_test(NAME pic_text_receiver COMMAND PICTextReceiverTests -o -,txt
    -o "${CMAKE_CURRENT_BINARY_DIR}/pic-text-receiver.txt,txt")
add_executable(DeviceRepositoryTests tst_devicerepository.cpp)
set_target_properties(DeviceRepositoryTests PROPERTIES AUTOMOC ON)
# Communication is test-only: one decoded Response compatibility case.
target_link_libraries(DeviceRepositoryTests PRIVATE MaimanPBS::Model MaimanPBS::Communication Qt6::Test)
add_test(NAME device_repository COMMAND DeviceRepositoryTests -o -,txt
    -o "${CMAKE_CURRENT_BINARY_DIR}/device-repository.txt,txt")
set_tests_properties(communication_foundation serial_data_source data_thread pic_text_protocol pic_text_receiver device_repository PROPERTIES TIMEOUT 15)

# Make CTest find Qt and compiler runtime DLLs without a deployment step.
if(WIN32)
    set(runtime_environment
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
    if(MINGW)
        get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        list(APPEND runtime_environment "PATH=path_list_prepend:${compiler_bin}")
    endif()
    set_tests_properties(communication_foundation serial_data_source data_thread pic_text_protocol pic_text_receiver device_repository PROPERTIES
        ENVIRONMENT_MODIFICATION "${runtime_environment}"
    )
endif()
