sdk_generate_library(edgertc_bouffalo)

sdk_add_include_directories(..)

sdk_library_add_sources(
    session.c
    media.c
    audio_tx.c
    audio_rx.c
    video_tx.c
)

sdk_library_add_sources_ifdef(CONFIG_WEBRTC_VIDEO_RX
    video_rx.c
)
