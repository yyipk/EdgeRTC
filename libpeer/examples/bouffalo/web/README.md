# Browser camera to BL618DG LCD

`browser_video.js` adds a camera control beside the existing microphone button.
The camera starts automatically when the DataChannel opens, requesting permission
if needed. If permission is denied, allow camera access and tap the camera control
to retry. It prefers the front camera and uses its own video-only stream;
microphone capture and incoming DataChannel handlers stay intact.
Use HTTPS or localhost, and keep the `libpeer` channel unordered with
`maxPacketLifeTime: 1000`. SCTP can retransmit within this one-second message
lifetime, then abandon stale video with FORWARD_TSN. A retransmission count
alone does not bound the elapsed wait when retransmission timers back off.
The receiver never acknowledges missing TSNs merely because time elapsed;
FORWARD_TSN from the sender authorizes skipping them. Late tails whose BEGIN
was skipped are released once cumulatively acknowledged, while their receipt
state remains intact.

To deploy these files to the web server:

1. Back up `/var/www/webrtc/index.html`.
2. Copy `browser_video.js`, `browser_jpeg_worker.js`, `video_probe.js`,
   `call_controls.css`, and the `vendor/` directory to `/var/www/webrtc/`,
   preserving their paths. Serve `.js` as JavaScript and `.wasm` as
   `application/wasm`.
3. Preview and apply the integration patch (run from this directory):

   ```sh
   patch --dry-run -l /var/www/webrtc/index.html < browser_video.patch
   patch -l /var/www/webrtc/index.html < browser_video.patch
   patch --dry-run -l /var/www/webrtc/index.html < manual_connect.patch
   patch -l /var/www/webrtc/index.html < manual_connect.patch
   ```

4. Reload the page without cache, connect the device, and allow camera access.
   The button stops capture when clicked again; disconnecting stops tracks and
   removes the old control. Reconnect to create a new control.

Device discovery only updates the device list and busy status. A call starts only
after clicking Connect or Reconnect. Page load, a device becoming available, and
connection failures never start or retry a call automatically. Reload existing
tabs after deployment so they stop using the old automatic connection behavior.

For TURN diagnostics, open the page with `?video_debug=1`. Every five seconds,
`video_probe.js` reports cumulative camera send, DataChannel receive, render and
selected candidate pair counters through a HEAD request in the web access log.
Sent frames count successful enqueueing, not delivery. The probe is inactive
without this query parameter and does not change channel reliability.

The call controls use one compact row: round microphone, camera and speaker
toggles with on/off labels, followed by a red hang-up button.
A single-device call on desktop (1024 CSS pixels wide or more) fills the page
width in a 4:3 video area; the page scrolls vertically when needed. On mobile,
the call fills the viewport, with black bars where the aspect ratios differ.
Video keeps its aspect ratio without cropping. Controls float over the bottom
of the viewport, respecting safe areas. Microphone and speaker still require an
explicit tap; only video defaults to on.
Normal MJPEG/video status text is omitted. Frame rate, dropped frames and route
overlays remain visible, along with connection and camera errors.

The sender center-crops to 480×480 and targets 30 JPEG frames per second,
reducing quality or dropping frames above 60 KiB. It verifies baseline JPEG and
4:2:0 sampling and absence of restart coding before sending. Safari's native
encoder emits restart-coded JPEG, which the device parser rejects. When native
output is incompatible, a lazily loaded @jsquash/jpeg 1.6.0 worker encodes baseline
4:2:0 JPEG without restart markers. Compatible browsers retain native encoding.
Keep `optimize_coding` enabled: disabling it in this bundled encoder can produce
invalid JPEG entropy data. After updating the worker, reload without cache to
replace the old encoder configuration.
The worker processes one frame at a time and is terminated when capture stops.
The 30 FPS target is an upper limit; actual throughput depends on encoding and
channel backlog. These web assets consume no device firmware memory or Flash.

Each complete JPEG is sent as one binary DataChannel message, without an
application header or application fragmentation. SCTP handles fragmentation,
retransmission and message completion. Once all fragments are present, the board
copies their borrowed spans directly into one of two fixed JPEG `net_buf` slots;
there is no intermediate contiguous SCTP message copy. Decoding and display also
use fixed `net_buf` pools, with no processing-time heap allocation in this path.
The demo reserves 192 KiB for SCTP RX and advertises a 65535-byte message limit;
the camera sender keeps its stricter 60 KiB JPEG cap. Frames are skipped above
32 KiB of backlog and preflighted against a 96 KiB channel buffer limit.
A send failure stops capture. Deploy the sender and firmware together: the old
`BVJ1` protocol is no longer accepted by the video receiver.

The connection's fixed SCTP packet pool is initialized from the configured RX
descriptor capacity plus TX packet capacity and two temporary slots (168 slots
for this demo). It remains available across association resets so an in-flight
callback can safely return its borrowed input, and is freed at final peer teardown.
Small DATA fragments still retain whole packet slots; the RX budget also covers
the large-and-tiny fragment pairs produced by Chrome for a 60 KiB JPEG.
