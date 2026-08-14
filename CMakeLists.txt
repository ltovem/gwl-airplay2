cmake_minimum_required(VERSION 3.20)

project(gwl-airplay2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GWL_AIRPLAY2_BUILD_DEMO "Build the command line demo" ON)
option(GWL_AIRPLAY2_BUILD_TESTS "Build tests" OFF)

add_library(gwl-airplay2
    src/airplay_receiver.cpp
    src/http_server.cpp
    src/mdns.cpp
    src/rtsp.cpp
    src/rtp.cpp
    src/sdp.cpp
    src/crypto.cpp
)

target_include_directories(gwl-airplay2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gwl-airplay2 PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(gwl-airplay2 PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(gwl-airplay2 PRIVATE pthread)
endif()

if(GWL_AIRPLAY2_BUILD_DEMO)
    add_executable(gwl-airplay2-demo examples/receiver/main.cpp)
    target_link_libraries(gwl-airplay2-demo PRIVATE gwl-airplay2)
endif()

if(GWL_AIRPLAY2_BUILD_TESTS)
    enable_testing()
endif()
