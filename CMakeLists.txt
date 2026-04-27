cmake_minimum_required(VERSION 3.21)

project(MaimanPBS VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)

# ------------------------------------------------------------
# Main application
# ------------------------------------------------------------

add_executable(MaimanPBS
    src/main.cpp

    src/gui/MainWindow.h
    src/gui/MainWindow.cpp

    src/domain/DeviceModel.h
    src/domain/DeviceState.h
    src/domain/ChannelCommandResolver.h
    src/domain/ChannelCommandResolver.cpp

    src/application/IDeviceRepository.h
    src/application/use_cases/DeviceUseCases.h
    src/application/use_cases/DeviceUseCases.cpp

    src/infrastructure/XmlDeviceModelLoader.h
    src/infrastructure/XmlDeviceModelLoader.cpp
    src/infrastructure/MockDeviceRepository.h
    src/infrastructure/MockDeviceRepository.cpp
)

target_include_directories(MaimanPBS PRIVATE src)

target_link_libraries(MaimanPBS PRIVATE
    Qt6::Widgets
    Qt6::Core
)

target_compile_definitions(MaimanPBS PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

enable_testing()

function(add_maiman_test TEST_TARGET)
    add_executable(${TEST_TARGET}
        ${ARGN}
    )

    target_include_directories(${TEST_TARGET} PRIVATE src)

    target_link_libraries(${TEST_TARGET} PRIVATE
        Qt6::Core
    )

    target_compile_definitions(${TEST_TARGET} PRIVATE
        PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
endfunction()

add_maiman_test(ChannelCommandResolverTests
    tests/ChannelCommandResolverTests.cpp

    src/domain/DeviceModel.h
    src/domain/ChannelCommandResolver.h
    src/domain/ChannelCommandResolver.cpp
)

add_maiman_test(XmlDeviceModelLoaderTests
    tests/XmlDeviceModelLoaderTests.cpp

    src/domain/DeviceModel.h
    src/domain/ChannelCommandResolver.h
    src/domain/ChannelCommandResolver.cpp

    src/infrastructure/XmlDeviceModelLoader.h
    src/infrastructure/XmlDeviceModelLoader.cpp
)

add_maiman_test(MockDeviceRepositoryTests
    tests/MockDeviceRepositoryTests.cpp

    src/domain/DeviceModel.h
    src/domain/DeviceState.h

    src/application/IDeviceRepository.h

    src/infrastructure/MockDeviceRepository.h
    src/infrastructure/MockDeviceRepository.cpp
)

add_maiman_test(DeviceUseCasesTests
    tests/DeviceUseCasesTests.cpp

    src/domain/DeviceModel.h
    src/domain/DeviceState.h

    src/application/IDeviceRepository.h
    src/application/use_cases/DeviceUseCases.h
    src/application/use_cases/DeviceUseCases.cpp

    src/infrastructure/MockDeviceRepository.h
    src/infrastructure/MockDeviceRepository.cpp
)