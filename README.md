# EdgeRTC

EdgeRTC is a compact, high-performance WebRTC library for memory- and
flash-constrained IoT and embedded devices. It is built on libpeer and keeps
the protocol stack small enough for products that need real-time audio,
video, or data communication without a browser-sized runtime.

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

Embedded SDK integrations may include `libpeer/CMakeLists.txt` and the required
algorithm directories from their own component build files.

## Upstream and licenses

EdgeRTC is based on [libpeer](https://github.com/sepfy/libpeer). Local changes
from the upstream baseline are recorded in
[`libpeer/MODIFICATIONS.md`](libpeer/MODIFICATIONS.md).

libpeer and each bundled dependency retain their respective licenses. See the
license files in those directories before redistribution.
