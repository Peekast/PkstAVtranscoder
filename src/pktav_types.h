#ifndef _PKTAV_TYPES_H
#define _PKTAV_TYPES_H 1

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

typedef struct {
    int codec_type;
    AVCodec         *decode_codec;
    AVCodec         *encode_codec;
    AVStream        *output_stream;
    AVStream        *input_stream;
    AVCodecContext  *decode_ctx;
    AVCodecContext  *encode_ctx;
    AVFrame         *input_frame;
    AVFrame         *scale_frame;
    AVAudioFifo     *fifo;               /* Para hacer Resample de Audio */
    SwrContext      *resample_ctx;       /* Para hacer Resample de Audio */
    struct SwsContext *sws_ctx;
} TAVContext;

typedef struct {
    char    *codec;
    AVRational   framerate;
    int     width;
    int     height;
    int     gop_size;
    int     pix_fmt;
    char    *profile;
    char    *preset;
    int     crf;
    int     bitrate_bps;
} TAVConfigVideo;

typedef struct {
    char    *codec;
    int     bitrate_bps;
    int     channels;
    int     sample_rate;
} TAVConfigAudio;

/* 
 * Struct representing the configuration of the output. This includes
 * information about the destination, type of destination, key-value options, 
 * and a flag indicating whether to ignore failures or not.
 */
typedef struct {
    char *dst;                // A string indicating the destination of the data.
    char *dst_type;           // Type of the output (for instance, format or protocol type).
    char *kv_opts;            // Key-Value options to apply on output.
} TAVConfigFormat;

typedef struct {
    int  status;                     // Numeric status value
    char *status_desc;               // Status description
    long proc_time_ms;               // Processing time in milliseconds
    long time_left_ms;               // Approximate remaining time in milliseconds
    int  progress_pct;               // Progress percentage (integer)
    int  audio_pkts_read;            // Audio packets read
    int  video_pkts_read;            // Video packets read
    char *err_msg;                   // Error message (if any)
} TAVStatus;

extern void dump_TAVConfigVideo(TAVConfigVideo *videoConfig);
extern void dump_TAVConfigAudio(TAVConfigAudio *audioConfig);
extern void dump_TAVConfigFormat(TAVConfigFormat *formatConfig);

#endif