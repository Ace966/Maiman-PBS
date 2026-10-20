cmake_minimum_required(VERSION 3.22)

project(MaimanPBS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.9.3 REQUIRED COMPONENTS Core SerialPort)

add_subdirectory(src/communication)
add_subdirectory(src/model)
add_subdirectory(src/config)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.9.3 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
