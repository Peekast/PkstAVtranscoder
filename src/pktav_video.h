#ifndef _PKTAV_VIDEO_H
#define _PKTAV_VIDEO_H 1

#include "pktav_types.h"
#include "pktav_mediainfo.h"

extern int pktav_worker(int socket, const char *input, TAVInfo *mi, TAVConfigFormat *config_fmt, TAVConfigAudio *config_audio, TAVConfigVideo *config_video);

#endif