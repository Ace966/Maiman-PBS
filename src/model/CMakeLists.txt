add_library(maiman_pbs_model STATIC
    DeviceConfiguration.h
    DeviceRepository.h
    DeviceRepository.cpp
)
add_library(MaimanPBS::Model ALIAS maiman_pbs_model)
target_compile_features(maiman_pbs_model PUBLIC cxx_std_17)
target_include_directories(maiman_pbs_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(maiman_pbs_model PUBLIC Qt6::Core)
