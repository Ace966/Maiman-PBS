add_library(maiman_pbs_comm STATIC
    IDataSource.h
    serialdatasource.h
    serialdatasource.cpp
    DataThread.h
    DataThread.cpp
)
set_target_properties(maiman_pbs_comm PROPERTIES AUTOMOC ON)
add_library(MaimanPBS::Communication ALIAS maiman_pbs_comm)

target_compile_features(maiman_pbs_comm PUBLIC cxx_std_17)
target_include_directories(maiman_pbs_comm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(maiman_pbs_comm PUBLIC Qt6::Core Qt6::SerialPort)
