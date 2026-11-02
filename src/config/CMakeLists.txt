add_library(maiman_pbs_xml STATIC TreeItem.h TreeItem.cpp XmlParser.h XmlParser.cpp)
add_library(MaimanPBS::Xml ALIAS maiman_pbs_xml)

target_compile_features(maiman_pbs_xml PUBLIC cxx_std_17)
target_include_directories(maiman_pbs_xml PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(maiman_pbs_xml PUBLIC Qt6::Core)

add_library(maiman_pbs_config STATIC XmlConfigLoader.h XmlConfigLoader.cpp)
add_library(MaimanPBS::Config ALIAS maiman_pbs_config)
target_compile_features(maiman_pbs_config PUBLIC cxx_std_17)
target_include_directories(maiman_pbs_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(maiman_pbs_config PUBLIC MaimanPBS::Model)
