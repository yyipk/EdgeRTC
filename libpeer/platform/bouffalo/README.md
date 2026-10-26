# Bouffalo WebRTC platform

This directory connects libpeer to the Bouffalo SDK's Media Graph endpoints and
runs the WebRTC session workers. libpeer's protocol implementation remains in
`src/`; the example supplies board configuration, commands, and LVGL pages.
Each implementation has a matching header.

The Bouffalo port uses FreeRTOS tasks, queues, event groups, and mutexes
directly. Media Graph supplies the media endpoints and processing pipelines.

| File | Direction and responsibility |
| --- | --- |
| `session.c` | Public session lifecycle, call/hangup, signaling, PeerConnection, and network/media workers |
| `media.c` | Shared initialization, teardown, and PCMA format validation |
| `audio_tx.c` | Microphone → channel conversion → AEC/optional NS → PCMA → stream buffer; optional PCM WebSocket diagnostics |
| `audio_rx.c` | Received PCMA → packet queue → jitter buffer/PLC → PCM → channel conversion → speaker |
| `video_tx.c` | V4L2 MJPEG → Media Graph → output queue; retains camera buffers until the sender releases them |
| `video_rx.c` | SCTP JPEG fragments → complete JPEG queue → hardware MJPEG decoder → NV12 buffers → LCD |

Include `bouffalo/session.h` for the `webrtc_open()`/`webrtc_start()`/
`webrtc_stop()`/`webrtc_close()` handle API. `media_config.h` defines shared
audio/camera configuration; `media.h` holds shared media state and lifecycle.
`audio_tx.h`, `audio_rx.h`, and `video_tx.h` declare their respective
capture/playback and queue APIs. The session sends captured packets through
libpeer and feeds received packets back into these APIs.

`video_rx.c` directly manages three fixed 480 × 480 NV12 surfaces and switches
their Y/UV addresses in the existing OSD SEOF hook. It retains both in-flight
address banks until replacement and leaves the panel backlight to the application.

The example keeps LVGL initialization, OSD flush policy, page creation, and
widget updates in `app/app_ui.c`. After initializing the panel/OSD port, it calls
`webrtc_video_rx_display_init()`. Its LVGL timer reads the active state and frame
counter through `webrtc_video_rx_get_display_stats()` and calculates FPS once
per second. The RX worker emits LCD diagnostics. When video stops, the
example makes its page background opaque to cover the retained last frame.
On the first new video handoff it restores transparency, revealing the video
under the UI. All LVGL calls stay in the display task.

Serialize receive input and open/close calls with the peer mutex. Destroy the
receiver after stopping its input callbacks; it joins the decoder worker before
freeing resources. Queue depths, buffer counts, and task priorities remain in
the corresponding TX/RX implementation.

The Bouffalo example enables `CONFIG_EDGERTC_PLATFORM_BOUFFALO` with
`CONFIG_MEDIA_GRAPH` and builds this repository's libpeer while leaving the
SDK's built-in `CONFIG_LIBPEER` disabled.
`CONFIG_WEBRTC_VIDEO_RX` additionally builds `video_rx.c`;
`CONFIG_XWEBSOCKET` enables the audio diagnostic export.
