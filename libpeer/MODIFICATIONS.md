# libpeer modification record

## Baseline

- LiteRTC baseline: `6c80bd64d1f995e743db65ae1918f38c066f2ddf`
- Original libpeer revision recorded by LiteRTC: `9319aa434cb9e893f`
- Third-party dependencies remain single Git submodules at the LiteRTC root.

## Local changes

| Commit subject | Purpose |
| --- | --- |
| `build: integrate libpeer with embedded SDK` | Add the SDK build entry and use the LiteRTC root dependency submodules. |
| `build(sdk): use platform signaling dependencies` | Select external HTTP and MQTT through libpeer transport abstractions without platform conditionals in signaling code. `CONFIG_LIBPEER_USE_EXTERNAL_TRANSPORT` selects external or bundled core backends; libpeer has no RTSP call path, so the SDK RTSP server is not linked. |
| `fix(ice): compare candidate addresses correctly` | Compare address family, port, and IP instead of treating every address as equal. |
| `fix(port): adapt signaling waits for FreeRTOS` | Avoid conflicting signal headers and shorten blocking mDNS resolution. |
| `fix(tls): make transport lifecycle robust` | Support nonblocking MQTT reads and release partial connections on failure. |
| `perf(signaling): share protocol workspace` | Reuse one static workspace for mutually exclusive HTTP and MQTT operations. |
| `fix(signaling): validate bounded JSON-RPC requests` | Parse bounded MQTT payloads, validate field types, and ignore response messages. |
| `fix(signaling): manage MQTT connection lifecycle` | Propagate connection failures and make repeated connect/disconnect safe. |
| `fix(signaling): isolate MQTT sessions by device` | Derive a unique MQTT client ID from the active network interface MAC address. |
| `fix(stun): track transactions and ICE attributes` | Generate per-request transaction IDs and parse priority, role, nomination, and error attributes. |
| `feat(ice): support triggered peer-reflexive checks` | Create prflx candidates from authenticated inbound checks and validate candidate-pair responses. |
| `fix(ice): verify consent on selected pairs` | Send periodic authenticated Binding requests and close connections whose selected pair no longer responds. |
| `perf(transport): drain queued datagrams per loop` | Process up to 16 queued datagrams per peer loop iteration. |
| `fix(dtls): release and rebuild session state safely` | Release SRTP, DTLS cookie, SCTP, and fingerprint state across reconnects. |
| `fix(dtls): reassemble fragmented ClientHello records` | Reassemble fragmented DTLS ClientHello records before passing them to the TLS stack. |
| `fix(signaling): ignore duplicate remote answers` | Prevent repeated answers from restarting an active negotiation. |
| `feat(signaling): add device-to-device calls` | Address devices by MAC-derived ID and exchange session-scoped offer, answer, busy, and hangup messages without changing the browser RPC topics. |
| `feat(rtp): expose packet metadata to media callbacks` | Deliver RTP sequence number, timestamp, and SSRC to audio and video callbacks. |
| `fix(rtp): parse variable headers and rebuild H264 safely` | Parse CSRC, extension, and padding fields; add bounded STAP-A and FU-A handling with per-decoder state. |

## Verification

The RTP payload test covers variable headers, metadata, decoder isolation,
size limits, STAP-A, invalid FU-A sequences, and decoder buffer reuse. The SDK
`webrtc_solution` build verifies that external signaling resolves
through the SDK HTTP, MQTT, mbedTLS, and cJSON libraries.
