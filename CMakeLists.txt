cmake_minimum_required(VERSION 3.20)

project(gwl-airplay2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GWL_AIRPLAY2_BUILD_DEMO "Build the command line demo" ON)
option(GWL_AIRPLAY2_BUILD_TESTS "Build tests" OFF)
option(GWL_AIRPLAY2_BUILD_SHARED "Build a shared library" OFF)

# The protocol core is deliberately platform-neutral. Native applications
# provide their own AudioSink and lifecycle integration through the public API.
set(GWL_AIRPLAY2_SOURCES
    src/airplay_receiver.cpp
    src/http_server.cpp
    src/mdns.cpp
    src/rtsp.cpp
    src/rtp.cpp
    src/rtp_jitter_buffer.cpp
    src/media_frame.cpp
    src/sdp.cpp
    src/crypto.cpp
    src/crypto_session.cpp
    src/audio_pipeline.cpp
    src/audio_clock.cpp
    src/callback_audio_sink.cpp
    src/platform.cpp
    src/platform_audio.cpp
    src/alac_config.cpp
    src/alac_rtp.cpp
    src/alac_decoder.cpp
)

if(APPLE)
    list(APPEND GWL_AIRPLAY2_SOURCES src/apple_audio_sink.cpp)
endif()

if(GWL_AIRPLAY2_BUILD_SHARED)
    add_library(gwl-airplay2 SHARED ${GWL_AIRPLAY2_SOURCES})
else()
    add_library(gwl-airplay2 STATIC ${GWL_AIRPLAY2_SOURCES})
endif()

target_include_directories(gwl-airplay2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gwl-airplay2 PUBLIC cxx_std_17)

target_compile_definitions(gwl-airplay2
    PUBLIC
        GWL_AIRPLAY2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        GWL_AIRPLAY2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        GWL_AIRPLAY2_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(WIN32)
    target_link_libraries(gwl-airplay2 PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(gwl-airplay2 PRIVATE pthread)
elseif(APPLE)
    target_link_libraries(gwl-airplay2 PRIVATE "-framework AudioToolbox")
endif()

# The command-line demo is a host application. It is intentionally excluded
# from iOS and Android builds, where the host lifecycle belongs to the app.
if(GWL_AIRPLAY2_BUILD_DEMO AND NOT IOS AND NOT ANDROID)
    add_executable(gwl-airplay2-demo examples/receiver/main.cpp)
    target_link_libraries(gwl-airplay2-demo PRIVATE gwl-airplay2)
endif()

if(GWL_AIRPLAY2_BUILD_TESTS)
    enable_testing()
    add_executable(gwl-airplay2-tests tests/core_tests.cpp)
    target_link_libraries(gwl-airplay2-tests PRIVATE gwl-airplay2)
    add_test(NAME gwl-airplay2-core COMMAND gwl-airplay2-tests)
endif()

# Installation is useful for desktop packages and for consumers that build
# the core as an iOS/Android native dependency.
include(GNUInstallDirs)
install(TARGETS gwl-airplay2
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Platform sanity checks. These do not pull native UI/audio frameworks into
# the protocol core; they only make the intended cross-build targets explicit.
if(IOS)
    message(STATUS "gwl-airplay2: iOS build (${CMAKE_OSX_SYSROOT})")
elseif(ANDROID)
    message(STATUS "gwl-airplay2: Android build (${CMAKE_ANDROID_ARCH_ABI}, API ${CMAKE_SYSTEM_VERSION})")
endif()
