cmake_minimum_required(VERSION 3.20)

project(gwl-airplay2 VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GWL_AIRPLAY2_BUILD_DEMO "Build the command line demo" ON)
option(GWL_AIRPLAY2_BUILD_TESTS "Build tests" OFF)
option(GWL_AIRPLAY2_BUILD_SHARED "Build a shared library" OFF)
option(GWL_AIRPLAY2_ENABLE_OPENSSL "Enable OpenSSL crypto backend" ON)
option(GWL_AIRPLAY2_USE_BUNDLED_OPENSSL "Build a private static OpenSSL from source when no system OpenSSL is available" OFF)
set(GWL_AIRPLAY2_OPENSSL_GIT_TAG "openssl-3.5.4" CACHE STRING "Pinned OpenSSL source tag used by the bundled backend")

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
    src/airplay_pairing.cpp
    src/audio_pipeline.cpp
    src/alac_audio_pipeline.cpp
    src/audio_clock.cpp
    src/callback_audio_sink.cpp
    src/platform.cpp
    src/platform_audio.cpp
    src/alac_config.cpp
    src/alac_rtp.cpp
    src/alac_decoder.cpp
    third_party/alac/codec/EndianPortable.c
    third_party/alac/codec/ALACBitUtilities.c
    third_party/alac/codec/ALACDecoder.cpp
    third_party/alac/codec/ag_dec.c
    third_party/alac/codec/dp_dec.c
    third_party/alac/codec/matrix_dec.c
)

if(APPLE)
    list(APPEND GWL_AIRPLAY2_SOURCES src/apple_audio_sink.cpp)
endif()

# Crypto is an implementation detail of the library. Prefer an already
# installed OpenSSL, but allow applications/CI to opt into a pinned source
# build. The bundled copy is static and PRIVATE, so it is not exposed through
# the public gwl-airplay2 headers and does not replace another application's
# OpenSSL runtime.
if(GWL_AIRPLAY2_ENABLE_OPENSSL)
    find_package(OpenSSL QUIET COMPONENTS Crypto)

    if(NOT OpenSSL_FOUND AND GWL_AIRPLAY2_USE_BUNDLED_OPENSSL)
        include(FetchContent)
        message(STATUS "gwl-airplay2: fetching OpenSSL ${GWL_AIRPLAY2_OPENSSL_GIT_TAG} from source")
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(OPENSSL_BUILD_APPS OFF CACHE BOOL "" FORCE)
        set(OPENSSL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(OPENSSL_BUILD_FUZZ_TESTS OFF CACHE BOOL "" FORCE)
        set(OPENSSL_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
        FetchContent_Declare(
            gwl_openssl
            GIT_REPOSITORY https://github.com/openssl/openssl.git
            GIT_TAG ${GWL_AIRPLAY2_OPENSSL_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(gwl_openssl)
        find_package(OpenSSL REQUIRED COMPONENTS Crypto)
        set(GWL_AIRPLAY2_BUNDLED_OPENSSL TRUE)
    endif()
endif()

if(GWL_AIRPLAY2_BUILD_SHARED)
    add_library(gwl-airplay2 SHARED ${GWL_AIRPLAY2_SOURCES})
else()
    add_library(gwl-airplay2 STATIC ${GWL_AIRPLAY2_SOURCES})
endif()

target_include_directories(gwl-airplay2
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/alac_compat
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/alac/codec
)

target_compile_features(gwl-airplay2 PUBLIC cxx_std_17)

target_compile_definitions(gwl-airplay2
    PUBLIC
        GWL_AIRPLAY2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        GWL_AIRPLAY2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        GWL_AIRPLAY2_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(OpenSSL_FOUND)
    target_link_libraries(gwl-airplay2 PRIVATE OpenSSL::Crypto)
    target_compile_definitions(gwl-airplay2 PRIVATE GWL_AIRPLAY2_HAS_OPENSSL=1)
    if(GWL_AIRPLAY2_BUNDLED_OPENSSL)
        target_compile_definitions(gwl-airplay2 PRIVATE GWL_AIRPLAY2_BUNDLED_OPENSSL=1)
        message(STATUS "gwl-airplay2: private bundled OpenSSL crypto backend enabled")
    else()
        message(STATUS "gwl-airplay2: system OpenSSL crypto backend enabled")
    endif()
else()
    message(STATUS "gwl-airplay2: OpenSSL not found; AirPlay pairing backend will be unavailable")
endif()

if(WIN32)
    target_link_libraries(gwl-airplay2 PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(gwl-airplay2 PRIVATE pthread)
elseif(APPLE)
    target_link_libraries(gwl-airplay2 PRIVATE "-framework AudioToolbox")
endif()

if(GWL_AIRPLAY2_BUILD_DEMO AND NOT IOS AND NOT ANDROID)
    add_executable(gwl-airplay2-demo examples/receiver/main.cpp)
    target_link_libraries(gwl-airplay2-demo PRIVATE gwl-airplay2)
endif()

if(GWL_AIRPLAY2_BUILD_DEMO AND APPLE AND NOT IOS AND NOT TVOS)
    add_executable(gwl-airplay2-macos-demo examples/macos/main.cpp)
    target_link_libraries(gwl-airplay2-macos-demo PRIVATE gwl-airplay2)

    enable_language(OBJCXX)
    add_executable(gwl-airplay2-macos-ui MACOSX_BUNDLE examples/macos/GwlAirPlayUI.mm)
    target_link_libraries(gwl-airplay2-macos-ui PRIVATE gwl-airplay2 "-framework AppKit" "-framework Foundation")

    set_source_files_properties(examples/macos/GwlAirPlayUI.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )

    set_target_properties(gwl-airplay2-macos-ui PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "GWL AirPlay Receiver"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.gwl.airplay2.receiver"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    )
endif()

if(GWL_AIRPLAY2_BUILD_TESTS)
    enable_testing()
    add_executable(gwl-airplay2-tests tests/core_tests.cpp)
    target_link_libraries(gwl-airplay2-tests PRIVATE gwl-airplay2)
    add_test(NAME gwl-airplay2-core COMMAND gwl-airplay2-tests)
endif()

include(GNUInstallDirs)
install(TARGETS gwl-airplay2
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(IOS)
    message(STATUS "gwl-airplay2: iOS build (${CMAKE_OSX_SYSROOT})")
elseif(ANDROID)
    message(STATUS "gwl-airplay2: Android build (${CMAKE_ANDROID_ARCH_ABI}, API ${CMAKE_SYSTEM_VERSION})")
endif()
