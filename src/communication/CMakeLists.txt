# An interface target carries dependencies until transport sources are approved.
add_library(maiman_pbs_comm INTERFACE)
add_library(MaimanPBS::Communication ALIAS maiman_pbs_comm)

target_compile_features(maiman_pbs_comm INTERFACE cxx_std_17)
target_link_libraries(maiman_pbs_comm INTERFACE Qt6::Core Qt6::SerialPort)
