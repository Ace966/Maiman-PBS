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
<<<<<<< Updated upstream
    src/infrastructure/XmlDeviceModelLoader.h
    src/infrastructure/XmlDeviceModelLoader.cpp
=======
    src/domain/DeviceState.h
    src/application/IDeviceRepository.h
    src/infrastructure/XmlDeviceModelLoader.h
    src/infrastructure/XmlDeviceModelLoader.cpp
    src/infrastructure/MockDeviceRepository.h
    src/infrastructure/MockDeviceRepository.cpp
    src/application/use_cases/DeviceUseCases.h
    src/application/use_cases/DeviceUseCases.cpp
>>>>>>> Stashed changes
)

target_include_directories(MaimanPBS PRIVATE src)
target_link_libraries(MaimanPBS PRIVATE Qt6::Widgets Qt6::Core)
<<<<<<< Updated upstream

=======
target_compile_definitions(MaimanPBS PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
>>>>>>> Stashed changes
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
<<<<<<< Updated upstream

add_test(NAME XmlDeviceModelLoaderTests COMMAND XmlDeviceModelLoaderTests)
=======
target_compile_definitions(XmlDeviceModelLoaderTests PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME XmlDeviceModelLoaderTests COMMAND XmlDeviceModelLoaderTests)

add_executable(MockDeviceRepositoryTests
    tests/MockDeviceRepositoryTests.cpp
    src/domain/DeviceModel.h
    src/domain/DeviceState.h
    src/application/IDeviceRepository.h
    src/infrastructure/MockDeviceRepository.h
    src/infrastructure/MockDeviceRepository.cpp
)

target_include_directories(MockDeviceRepositoryTests PRIVATE src)
target_link_libraries(MockDeviceRepositoryTests PRIVATE Qt6::Core)

add_test(NAME MockDeviceRepositoryTests COMMAND MockDeviceRepositoryTests)

add_executable(DeviceUseCasesTests
    tests/DeviceUseCasesTests.cpp
    src/domain/DeviceModel.h
    src/domain/DeviceState.h
    src/application/IDeviceRepository.h
    src/application/use_cases/DeviceUseCases.h
    src/application/use_cases/DeviceUseCases.cpp
    src/infrastructure/MockDeviceRepository.h
    src/infrastructure/MockDeviceRepository.cpp
)

target_include_directories(DeviceUseCasesTests PRIVATE src)
target_link_libraries(DeviceUseCasesTests PRIVATE Qt6::Core)

add_test(NAME DeviceUseCasesTests COMMAND DeviceUseCasesTests)
>>>>>>> Stashed changes
