cmake_minimum_required(VERSION 3.21)

project(MaimanPBS VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)

add_executable(MaimanPBS
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/domain/DeviceModel.h
    src/domain/ChannelCommandResolver.h
    src/domain/ChannelCommandResolver.cpp
    src/infrastructure/XmlDeviceModelLoader.h
    src/infrastructure/XmlDeviceModelLoader.cpp
)

target_include_directories(MaimanPBS PRIVATE src)
target_link_libraries(MaimanPBS PRIVATE Qt6::Widgets Qt6::Core)

enable_testing()

add_executable(ChannelCommandResolverTests
    tests/ChannelCommandResolverTests.cpp
    src/domain/DeviceModel.h
    src/domain/ChannelCommandResolver.h
    src/domain/ChannelCommandResolver.cpp
)

target_include_directories(ChannelCommandResolverTests PRIVATE src)
target_link_libraries(ChannelCommandResolverTests PRIVATE Qt6::Core)

add_test(NAME ChannelCommandResolverTests COMMAND ChannelCommandResolverTests)

add_executable(XmlDeviceModelLoaderTests
    tests/XmlDeviceModelLoaderTests.cpp
    src/domain/DeviceModel.h
    src/domain/ChannelCommandResolver.h
    src/domain/ChannelCommandResolver.cpp
    src/infrastructure/XmlDeviceModelLoader.h
    src/infrastructure/XmlDeviceModelLoader.cpp
)

target_include_directories(XmlDeviceModelLoaderTests PRIVATE src)
target_link_libraries(XmlDeviceModelLoaderTests PRIVATE Qt6::Core)

add_test(NAME XmlDeviceModelLoaderTests COMMAND XmlDeviceModelLoaderTests)