# GWL AirPlay 2 Receiver

Cross-platform C++ AirPlay receiver project.

## Status

### Phase 1 — foundation

- [x] CMake/C++17 project
- [x] Cross-platform TCP server abstraction
- [x] Basic HTTP request/response handling
- [x] Receiver lifecycle API
- [x] `/info` endpoint
- [x] Initial mDNS/DNS-SD AirPlay service advertisement
- [ ] Query-driven mDNS responder and full DNS-SD record set
- [ ] Full AirPlay RTSP state machine
- [ ] Pairing/authentication
- [ ] Encrypted audio
- [ ] Audio decoding/output
- [ ] Video streaming/decoding/output
- [ ] Real-device AirPlay 2 interoperability tests

The current code is deliberately a foundation, not yet a complete AirPlay 2 implementation.

## Build

```bash
cmake -S . -B build
cmake --build build -j
./build/gwl-airplay2-demo
```

On Windows, run the generated executable from the build directory.

## Architecture

The project keeps discovery, protocol/session handling, cryptography, media decoding, and platform rendering separate so that Windows, macOS, and Linux can share the core receiver implementation.

## macOS

A native macOS demo target will be added after the protocol foundation is stable. It will use the same receiver core rather than duplicating AirPlay protocol logic.
