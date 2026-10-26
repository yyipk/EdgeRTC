# WebRTC

This example implements two-way PCMA audio and DVP MJPEG video between a
Bouffalo board and a browser. The application records local PCM with
`multimedia/xrecord`, sends encoded audio through libpeer, plays browser audio
through a Media Graph pipeline, and sends hardware-encoded JPEG frames over a
DataChannel. On BL618DG it also receives browser JPEG frames and displays them
through MJDEC and the ST7102 DSI LCD.

The public API owns the WebRTC and worker-task lifecycle. Its implementation
directly uses the Bouffalo FreeRTOS and Media Graph services used by this
example.

It boots WebRTC directly. The MicroPixel App Host is a separate example at
`examples/solution/micropixel_solution` in the Bouffalo SDK.

After `wifi_sta_connect <ssid> <password>` obtains an IP address, the product
saves the successful credentials together in `PSM` and reconnects with them on
the next boot. Existing separate Wi-Fi settings remain readable; credentials
are not compiled into the firmware.

## Audio pipeline

```text
codec capture (PCM S16LE, interleaved reference + microphone channels)
  -> xrecord
  -> Media Graph node pipeline
       -> channel converter when device channels differ from AEC input
       -> common AEC node (bypass, AEC4, or SpeexDSP backend)
       -> sample-rate conversion / optional noise suppression
       -> PCMA encoder
       -> MSP stream buffer node (PCMA)
  -> rtc_media_tx task
  -> libpeer RTP/SRTP
  -> browser

browser PCMA
  -> RTP jitter buffer and packet-loss concealment
  -> Media Graph node pipeline
       -> PCMA decoder
       -> mono-to-stereo channel converter
       -> PCM audio-output sink
  -> codec speaker
```

The video paths are:

```text
DVP camera -> hardware MJPEG encoder -> net_buf referencing the captured frame
  -> WebRTC pipeline: DataChannel chunks -> browser frame reassembly -> Canvas

browser camera -> Canvas JPEG (480x480) -> one DataChannel message per JPEG
  -> SCTP reassembly -> one of two fixed JPEG net_buf slots
  -> video_rx worker / Media Graph pipeline
     -> MJDEC node -> one of three NV12 buffers -> LCD sink
        -> DPI YUV base + LVGL OSD -> ST7102 DSI LCD
```

The final `net_buf_unref()` returns the captured frame to V4L2.

### Browser video on BL618DG

`CONFIG_WEBRTC_VIDEO_RX=y` selects the hardware decoder and LVGL OSD layer.
`webrtc_cfg_t.video_receive_enabled` enables reception independently of local
camera capture. Build with `make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap`.
The browser sender and page integration are in [web/](web/README.md).

SCTP retains owner-backed fragments until a complete message is available, then
delivers them in TSN order. The receiver copies these borrowed spans directly
into one aligned 60 KiB JPEG slot, without intermediate SCTP coalescing or
application-level chunk headers. Queue entries are `net_buf *` references.
SCTP caches, JPEG slots and display surfaces use fixed `net_buf` pools, with
no processing-time heap allocation. A single worker runs the decoder and LCD sink; pipeline
workspaces contain descriptors, not pixels. The decoder releases the JPEG
lease after stopping MJDEC, independently of the displayed frame's lifetime.

Three fixed 480x480 NV12 buffers use 1012.5 KiB; together with two JPEG slots this is
1132.5 KiB plus alignment, descriptors, and task/queue overhead, excluding SCTP
caches. Each NV12 `net_buf` allocation contains its frame metadata and pixels.
MJDEC writes directly into the free surface, and the sink transfers
that surface's reference to LCD scanout without a pixel copy. The sink clears
stale OSD SEOF and controller pending requests before queuing the surface, all
with interrupts locked. The following OSD SEOF programs its Y/UV addresses.
Following the DSI path in `lvgl_v9_with_osd/dpi_manager.c`, the two latest address
sets retain their references because the switch takes effect one frame later;
the third surface remains available to MJDEC. The initial black surface stays
held through the first submission, and an idle SEOF does not release either
in-flight surface.
LVGL uses its separate ARGB8888 OSD buffers in double-buffered DIRECT mode.
It redraws and synchronizes dirty regions. An example-local flush callback
delegates only the final region to the original port, retaining the BSP's full
buffer cache clean and SEOF wait. The FPS label changes only when its displayed
value changes; LCD and LVGL common code is unchanged.

NV12 pixels occupy isolated cache lines. Startup cleans and invalidates all three
surfaces once. The receiver uses the decoder's default per-frame output cache
maintenance: clean/invalidate before DMA and invalidate after MJDEC is stopped.
MJDEC uses INCR4 bursts. Table loading uses validated parser
output so combined or separate DQT/DHT segments and optimized Huffman tables reach
the driver intact; the SDK header helpers assume one table per segment.
Its completion ISR only clears the flag and notifies the worker; the
worker stops, pops and clears the decoder. Each attempt waits 6 ms and retries
the same owned JPEG, with at most eleven attempts before reset and rejection.
This bounds bad-frame recovery instead of the example's indefinite retry loop.
Normal frames no longer reset MJDEC. Startup and destruction still reset it,
and the BL618DG B0 header-addressing branch follows the SDK's `mjdec_dht` example.

Busy reception drops a whole frame. Incomplete or stale frames, unsupported
JPEGs, and decoder timeouts are discarded. Hangup invalidates queued work;
the LCD backlight is turned off. An already submitted display frame completes
its handoff while hidden, retaining both in-flight references until later replacements.
A new call turns the backlight on only after a fresh submission reaches the
following OSD SEOF.
Stopping WebRTC waits for the decoder worker before freeing JPEG storage.

While the receive channel is open, the `video_rx` worker prints `[video-rx]`
statistics for the most recent reporting interval, approximately five seconds.
`window` gives its actual duration; all event counts cover only that interval:

- `messages`, `complete`, `submitted`: SCTP messages presented to the receiver,
  validated JPEGs, and frames accepted by the LCD sink. `submitted` is
  an acceptance count; the on-screen FPS counts base-address bank writes at SEOF,
  not confirmed physical scanout consumption or panel refreshes.
- `complete_fps`, `submit_fps`: fully assembled JPEGs per second (the available
  input rate before decoding and display drops) and successful LCD handoffs
  per second, calculated using the actual interval duration.
- `incomplete`, `busy`, `invalid`, `queue_drop`, `surface_drop`, `pipeline_fail`,
  `span_reject`: interrupted messages, JPEG-buffer allocation failures,
  invalid messages, queue failures, unavailable display surfaces,
  decoder/LCD pipeline errors, and rejected SCTP callback spans. `span_reject`
  also prints the total number of received fragment callbacks.

`[video-rx] stage=acquire/pipeline/present` adds buffer addresses and queue,
surface-acquire and pipeline times on errors or frames taking over 100 ms.
Repeated early errors are limited after the first three. `[video-lcd]` snapshots
active video each five seconds, or each FPS sample at zero FPS: `current` and
`next` identify the older and latest in-flight address sets, including each
surface's Y/UV addresses; neither identifies confirmed physical consumption.
`seof_delta` covers the latest FPS sample; handoff age and submit-to-SEOF delay use RTOS tick
resolution, so a zero delay alone does not establish an interrupt timing fault.
Diagnostic `t_ms` values use the same monotonic timer as `[mjpeg]`.
`[mjpeg]` distinguishes parse, DHT and timeout failures. First-frame and
retry diagnostics include `attempts`, accumulated setup/cache/wait times and
the final attempt's completion/ISR snapshot before stop/pop. `done_status` and
`irq_status` expose only the completion bit, not the full hardware status.
`total_us` ends at the final wait or table error; it includes previous attempts'
cleanup but excludes final cleanup, post-decode cache maintenance and printing.

These application counters describe complete SCTP messages and downstream
display drops. Incomplete messages abandoned within SCTP never reach this
receiver, so these counters do not measure network packet loss.

The reference configuration uses `640x480`, JPEG quality 50, and limits the
capture rate to 20 frames per second. The DataChannel is unordered and allows
retransmission within a one-second message lifetime. In the browser-to-device direction, SCTP handles
fragment recovery before presenting each complete JPEG message to the receiver.

The current audio format is:

- BL618DG/JY6316: 8 kHz capture and playback; four interleaved capture channels:
  channel 0 is codec reference, channel 1 is unused, and channels 2/3 are stereo
  microphones. The Media Graph converter preserves reference 0 and averages
  microphones 2/3 before AEC at 8 kHz. Device audio and PCMA use the same rate,
  so their pipeline needs no sample-rate conversion;
- BL616: 8 kHz capture and playback, with reference 0 and microphone 1;
- 16-bit signed little-endian PCM capture and stereo PCM playback. The playback
  pipeline duplicates decoded mono to stereo. JY6316 keeps four TDM slots on
  the wire, with the left/right pair repeated in the four 32-bit FIFO words;
- 20 ms per capture frame (1280 bytes PCM on BL618DG, 640 bytes on BL616);
- 8 kHz PCMA transport, 160 bytes per 20 ms packet.

The board audio port provides device channels and buffering through
`mg_xrecord_source_config_init()` and `mg_audio_sink_config_init()`. The
application sets the stream rate and frame format, then builds the pipeline
from these parameters without selecting a codec model. Channel mixing remains
in Media Graph; TDM packing and DMA remain in the driver.

The xrecord data-ready callback passes the interleaved PCM frame directly to
the capture pipeline. Registered nodes process it in order, and the terminal
stream-buffer node queues complete PCMA packets. The `rtc_media_tx` task reads the
pipeline output and calls libpeer, so the recording task never enters the
WebRTC state machine directly.
`mg_pipeline_create()` receives a media-type parameter; nodes are accepted
only when their media type matches the pipeline. Additional processing such as
noise suppression or gain can be inserted by registering another node at the
required position.

Select exactly one AEC backend at build time: `CONFIG_AEC4=y` or
`CONFIG_SPEEXDSP=y`. The application always uses the common Media Graph AEC
node and does not select an algorithm in source code. When both are disabled,
the bypass backend extracts the microphone channel without processing it.

## Architecture

Paths relative to this example directory:

```text
app/                     board startup, commands, and LVGL pages
config/                  application build configuration
tests/                   host-native portable-code tests
../../platform/bouffalo/  WebRTC session, audio/video adaptation, and video display
```

The SDK provides reusable xrecord, audio-output, and V4L2 endpoints in
`$BL_SDK_BASE/components/multimedia/media_graph/io/`, with OS and Bouffalo
adaptation in `$BL_SDK_BASE/components/multimedia/media_graph/platform/`.

[`session.h`](../../platform/bouffalo/session.h) exposes
the `webrtc_open()`/`webrtc_start()`/`webrtc_stop()`/`webrtc_close()` handle API.
`session.c` in `../../platform/bouffalo/` owns signaling,
PeerConnection, worker tasks, and their lifecycle. The same directory composes
the Media Graph pipelines, with a matching header for each implementation:
`audio_tx.c` captures and encodes audio, `audio_rx.c` buffers and plays it,
`video_tx.c` captures MJPEG. `video_rx.c` receives and decodes JPEGs, owns the
NV12 display buffers, and switches them at frame boundaries. `app/app_ui.c` owns
LVGL initialization, page creation, and widget timers; its display task services LVGL. A 250 ms LVGL
timer reads video state and the displayed-frame counter, updates the FPS label
once per second when needed, and makes the UI background opaque when video stops. The backlight belongs to the UI;
video teardown retains scanout buffers without hiding the interface.
The adapter includes neither application headers nor UI code. The session runs MQTT signaling,
`peer_connection_loop()`, and audio/video capture plus network send in the
`rtc_signaling`, `rtc_network_rx`, and `rtc_media_tx` tasks respectively. The Peer mutex
serializes `peer_connection_loop()`, signaling operations that mutate the Peer,
and DataChannel send. Audio RTP send remains independent, matching the libpeer
ESP32 example.

The current Bouffalo build opens `dvp0` through the multimedia V4L2 data model and
selects the hardware MJPEG format. Capture buffers use the common
`v4l2_dqbuf()`/`v4l2_qbuf()` ownership contract. It does not start or link
CherryUSB, EHCI ISO, or a UVC host.

## Media Graph integration

The current application targets BL616/BL618-class boards and uses:

- `multimedia/xrecord` raw PCM callbacks and the internal BL616 sound-card
  driver;
- XAV AO for stereo PCM speaker playback;
- FreeRTOS for polling and critical sections;
- lwIP, EasyFlash, and mbedTLS;
- the SDK MQTT-C and HTTP clients selected by libpeer's external transport backend;
- libpeer and SRTP for WebRTC media transport, with a configurable ICE server.

`CONFIG_XWEBSOCKET=y` supports the `webrtc_audio_ws` command, which exports PCM
before and after AEC for audio diagnostics. The firmware reads no media files
from ROMFS; Wi-Fi credentials and signaling settings use LittleFS instead.

## Run the example

### 1. Build and flash the board

Run the following commands from this example directory. Set `BL_SDK_BASE` to
the Bouffalo SDK checkout that provides the build system and board support.
The example adds this EdgeRTC checkout directly to its build and keeps the
SDK's built-in `CONFIG_LIBPEER` disabled.
For BL618DG with the ST7102 YDP395BT008-V1 480x480 MIPI DSI screen:

```sh
export BL_SDK_BASE=/path/to/bouffalo_sdk
make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap
```

For BL616:

```sh
make CHIP=bl616 BOARD=bl616dk
```

Flash `build/build_out/webrtc_bl616.bin` to the BL616 board.

This example uses the board's default partition table. When switching from the
former combined MicroPixel configuration, back up persistent data before
changing the partition table; `PSM` includes Wi-Fi and WebRTC server settings.
With `CONFIG_WEBRTC_VIDEO_RX=y`, the LCD receives browser video through MJDEC
and holds each NV12 reference until scanout retires it. With this option off,
the LCD displays the static LVGL UI without video decoding.

### 2. Select the WebRTC server

Ask the EdgeRTC server administrator to open the device administration page,
enter this board's MAC address, and provide the generated command. Run it in
the board shell:

```text
webrtc_server_set mqtts://<server-ip>:8883/<topic> <token> stun:<server-ip>:3478
```

This single command stores the MQTT signaling URL, MQTT authentication token,
and STUN URL, then restarts the board. It does not configure Wi-Fi.

To use UDP TURN instead, provide the long-term credential username and
password returned by the TURN service:

```text
webrtc_server_set mqtts://<server-ip>:8883/<topic> <token> turn:<server-ip>:3478 <turn-username> <turn-password>
```

`<token>` contains MQTT credentials generated specifically for this MAC. Do
not reuse another board's token or construct it from the signaling gateway's
internal MQTT account.

### 3. Connect Wi-Fi

After the board restarts, connect it to Wi-Fi:

```text
wifi_sta_connect <ssid> <password>
```

Wait for `Wi-Fi got IP`, `MQTT_Connect succeeded`, and `MQTT Subscribe
succeeded` in the board log.

### 4. Connect the browser

Wait for `MQTT_Connect succeeded`, then run `webrtc_id` in the board shell.
Open `https://<server-ip>/`, accept the self-signed certificate warning, enter
the displayed 12-digit Call ID, and click `Connect`. The browser does not need
the MQTT username or password. Allow microphone access for two-way audio. The
page starts remote audio muted, so unmute its audio control to hear the board.
DVP video is reassembled and rendered by the browser without server-side media
decoding.

The EdgeRTC page supports TURN relay fallback when P2P fails. The board's MQTT
token remains on the board and the signaling server; it is not sent to the
browser.

Successful WebRTC negotiation reaches `WebRTC state: completed`.

### Board-to-board call

Both boards must use the same MQTT base topic and ICE server. After both boards
report `MQTT Subscribe succeeded`, display their MAC-derived device IDs:

```text
webrtc_id
```

Start a call from one board with the other board's 12-digit ID. The receiving
board answers automatically:

```text
webrtc_call <device-id>
```

End the call from either board:

```text
webrtc_hangup
```

Device signaling uses `<base-topic>/device/<device-id>/signal`. Only one
browser or board call can use a board at a time; additional board offers receive
a `busy` response. Audio remains peer-to-peer when ICE finds a direct candidate
pair and falls back to TURN when relay credentials are configured.

Run the product configuration test on the host
from this example directory:

```sh
python3 tests/test_product_config.py
```

The other Python tests also run from `tests/`; those that use SDK Media Graph,
graphics, or BSP sources require `BL_SDK_BASE` as exported above. EdgeRTC and
example sources are located relative to the test scripts.
