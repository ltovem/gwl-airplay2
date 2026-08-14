# Mobile builds

`gwl-airplay2` keeps the AirPlay protocol implementation in a portable C++17 core. iOS and Android applications are expected to provide their native lifecycle, networking permissions, and audio output adapter through the public API.

## iOS

CMake can generate an Xcode project for both device and simulator builds. The repository CI validates arm64 device and arm64 simulator configurations.

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DGWL_AIRPLAY2_BUILD_DEMO=OFF \
  -DGWL_AIRPLAY2_BUILD_TESTS=OFF
cmake --build build-ios --config Release
```

## Android

Use the Android NDK CMake toolchain. The CI validates `arm64-v8a` and `x86_64` with API 24 and the static C++ runtime.

```sh
export ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
cmake -S . -B build-android \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static \
  -DGWL_AIRPLAY2_BUILD_DEMO=OFF \
  -DGWL_AIRPLAY2_BUILD_TESTS=OFF
cmake --build build-android --config Release
```

The core library intentionally does not link AVAudioEngine, AAudio, Oboe, WASAPI, ALSA, or PipeWire. Those platform integrations belong in application adapters so the same protocol core can be embedded in desktop and mobile applications.
