#ifndef _PKTVA_MEDIAINFO_H
#define _PKTVA_MEDIAINFO_H 1
#include <libavformat/avformat.h>
#include "pktav_keyvalue.h"

typedef struct {
    char   *format;                // Format of the media.
    double duration;               // Duration of the media.
    char   *video_codec;           // Video codec used in the media.
    char   *audio_codec;           // Audio codec used in the media.
    int    video_index;
    int    audio_index;
    int    width;                  // Width of the video in the media.
    int    height;                 // Height of the video in the media.
    int    video_bitrate_kbps;     // Bitrate of the video in the media in kbps.
    int    audio_bitrate_kbps;     // Bitrate of the audio in the media in kbps.
    double fps;                    // Frame rate of the video in the media.
    int    audio_channels;         // Number of audio channels in the media.
    int    sample_rate;            // Sample rate of the audio in the media.
    int    audio_packets;
    int    video_packets;
} TAVInfo;


extern int pktav_extract_mediainfo_from_file(const char *filename, TAVInfo **mi);
#endif
