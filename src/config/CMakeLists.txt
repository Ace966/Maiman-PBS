find_package(yaml-cpp 0.9.0 CONFIG REQUIRED)

add_library(maiman_pbs_config STATIC YamlConfigLoader.h YamlConfigLoader.cpp)
add_library(MaimanPBS::Config ALIAS maiman_pbs_config)
target_compile_features(maiman_pbs_config PUBLIC cxx_std_17)
target_include_directories(maiman_pbs_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(maiman_pbs_config PUBLIC MaimanPBS::Model PRIVATE yaml-cpp::yaml-cpp)
