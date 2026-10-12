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
set_tests_properties(communication_foundation serial_data_source PROPERTIES TIMEOUT 15)

# Make CTest find Qt and compiler runtime DLLs without a deployment step.
if(WIN32)
    set(runtime_environment
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
    if(MINGW)
        get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        list(APPEND runtime_environment "PATH=path_list_prepend:${compiler_bin}")
    endif()
    set_tests_properties(communication_foundation serial_data_source PROPERTIES
        ENVIRONMENT_MODIFICATION "${runtime_environment}"
    )
endif()
