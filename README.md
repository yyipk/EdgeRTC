# EdgeRTC

[中文介绍](README_zh-CN.md)

EdgeRTC is a lightweight, high-performance WebRTC library for IoT and embedded
devices, built on libpeer and focused on low RAM and flash usage and
low-latency real-time audio, video, and data communication. It retains the
core protocol capabilities required by device-side communication without the
overhead of a browser-sized runtime.

## Design goals

- Keep RAM and flash usage practical for constrained devices.
- Provide predictable processing and allocation behavior for real-time media.
- Minimize copies and drain queued network packets efficiently.
- Keep platform dependencies behind small socket, timing, and transport ports.
- Let products include only the media paths and algorithms they need.

## Capabilities

- ICE connectivity with STUN, TURN, nomination, and peer-reflexive candidates.
- DTLS-SRTP media security and SCTP data channels.
- RTP/RTCP paths for H.264, G.711 A-law, G.711 mu-law, and Opus media.
- WHIP and MQTT signaling options.
- Reconnect-safe transport lifecycle and bounded packet parsing.
- Optional compact PCMA jitter buffering and packet-loss concealment for
  constrained audio receivers.
- Optional embedded audio processing algorithms, including AEC4 acoustic echo
  cancellation.

EdgeRTC is intended for device-side WebRTC endpoints such as cameras,
intercoms, sensors, gateways, and other connected products. Applications
provide their own capture, playback, codec, networking, and platform glue as
needed by the target.

## Repository layout

```text
EdgeRTC/
|-- libpeer/          WebRTC transport and signaling implementation
|-- alg/audio/        Optional audio processing algorithms
|-- cJSON/            JSON dependency
|-- coreHTTP/         HTTP dependency
|-- coreMQTT/         MQTT dependency
|-- libsrtp/          SRTP dependency
|-- mbedtls/          TLS and DTLS dependency
`-- usrsctp/          SCTP dependency
```

## Building

The portable libpeer targets and examples can be configured directly:

```sh
cmake -S libpeer -B build
cmake --build build
```

The repository root `CMakeLists.txt` is the embedded SDK component entry. It
registers the enabled libpeer and audio algorithm targets and builds EdgeRTC
targets with link-time optimization.

## Upstream and licenses

EdgeRTC is based on [libpeer](https://github.com/sepfy/libpeer). Local changes
from the upstream baseline are recorded in
[`libpeer/MODIFICATIONS.md`](libpeer/MODIFICATIONS.md).

Original EdgeRTC code is available under the [MIT License](LICENSE). libpeer,
AEC4, and each bundled dependency retain their respective licenses. See the
license files in those directories before redistribution.
