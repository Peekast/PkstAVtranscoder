
#include "pktav_types.h"
#include "pktav_log.h"

void dump_TAVConfigVideo(TAVConfigVideo *videoConfig) {
    pktav_log(NULL, 0, "Video Config:\n");
    pktav_log(NULL, 0, "Codec: %s\n", videoConfig->codec);
    pktav_log(NULL, 0, "Framerate: %d/%d\n", videoConfig->framerate.num, videoConfig->framerate.den);
    pktav_log(NULL, 0, "Resolution: %dx%d\n", videoConfig->width, videoConfig->height);
    pktav_log(NULL, 0, "GOP Size: %d\n", videoConfig->gop_size);
    pktav_log(NULL, 0, "Pixel Format: %d\n", videoConfig->pix_fmt);
    pktav_log(NULL, 0, "Profile: %s\n", videoConfig->profile);
    pktav_log(NULL, 0, "Preset: %s\n", videoConfig->preset);
    pktav_log(NULL, 0, "CRF: %d\n", videoConfig->crf);
    pktav_log(NULL, 0, "Bitrate (bps): %d\n", videoConfig->bitrate_bps);
}

void dump_TAVConfigAudio(TAVConfigAudio *audioConfig) {
    pktav_log(NULL, 0, "Audio Config:\n");
    pktav_log(NULL, 0, "Codec: %s\n", audioConfig->codec);
    pktav_log(NULL, 0, "Bitrate (bps): %d\n", audioConfig->bitrate_bps);
    pktav_log(NULL, 0, "Channels: %d\n", audioConfig->channels);
    pktav_log(NULL, 0, "Sample Rate: %d\n", audioConfig->sample_rate);
}

void dump_TAVConfigFormat(TAVConfigFormat *formatConfig) {
    pktav_log(NULL, 0, "Format Config:\n");
    pktav_log(NULL, 0, "Destination: %s\n", formatConfig->dst);
    pktav_log(NULL, 0, "Destination Type: %s\n", formatConfig->dst_type);
    pktav_log(NULL, 0, "Key-Value Options: %s\n", formatConfig->kv_opts);
}
