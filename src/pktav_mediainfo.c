#include <libavformat/avformat.h>
#include <stdlib.h>
#include <stdio.h>
#include "pktav_mediainfo.h"
#include "pktav_strings.h"
#include "pktav_error.h"


static int pktav_count_packets(AVFormatContext *fmt, double *duration, int *audio_pkts, int *video_pkts) {
    if (!duration || !audio_pkts || !video_pkts)
        return -1;
    
    int64_t start_pts = AV_NOPTS_VALUE;
    int64_t end_pts   = AV_NOPTS_VALUE;
    int64_t end_pts_duration = AV_NOPTS_VALUE;
    AVPacket pkt;

    int video_stream_index = -1;

    for (int i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        return AVERROR_STREAM_NOT_FOUND;
    }
    
    while (av_read_frame(fmt, &pkt) >= 0) {
        if (pkt.stream_index == video_stream_index) {
            if (start_pts == AV_NOPTS_VALUE) {
                start_pts = pkt.pts;
            }
            end_pts = pkt.pts;
            end_pts_duration = pkt.duration;
            (*video_pkts)++;
        } else if (fmt->streams[pkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            (*audio_pkts)++;
        }
        av_packet_unref(&pkt);
    }

    AVStream* video_stream = fmt->streams[video_stream_index];
    if (start_pts != AV_NOPTS_VALUE && end_pts != AV_NOPTS_VALUE) {
        int64_t duration_pts = end_pts - start_pts + end_pts_duration;
        *duration = duration_pts * av_q2d(video_stream->time_base);
    } else {
        *duration = 0;
    }
   

    

    return 0;
}


static int pkst_extract_mediainfo_from_avformat(AVFormatContext *fmt, TAVInfo **mi) {
    AVStream *stream;
    int video_stream_index = -1;
    int audio_stream_index = -1;

    int i;

    if (!mi || !(*mi = pkst_alloc(sizeof(TAVInfo))))  {
        return AVERROR(ENOMEM);
    }

    (*mi)->format = pkst_alloc(strlen(fmt->iformat->name)+1);
    if ((*mi)->format == NULL) {
        return AVERROR(ENOMEM);
    }

    strcpy((*mi)->format, fmt->iformat->name);

    if (fmt->duration != AV_NOPTS_VALUE)
        (*mi)->duration = fmt->duration / AV_TIME_BASE;
    else 
        (*mi)->duration = -1;

    for (i = 0; i < fmt->nb_streams; i++) {
        stream = fmt->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            audio_stream_index = i;
        
        if (video_stream_index != -1 && audio_stream_index != -1)
            break;
    }

    (*mi)->audio_index = audio_stream_index;
    (*mi)->video_index = video_stream_index;

    if (video_stream_index != -1) {
        stream = fmt->streams[video_stream_index];
        (*mi)->width = stream->codecpar->width;

        (*mi)->height = stream->codecpar->height;

        (*mi)->fps = av_q2d(stream->avg_frame_rate);
        (*mi)->video_bitrate_kbps = stream->codecpar->bit_rate / 1000;

        (*mi)->video_codec = pkst_alloc(strlen(avcodec_get_name(stream->codecpar->codec_id)));
        if ((*mi)->video_codec != NULL) {
            strcpy((*mi)->video_codec, avcodec_get_name(stream->codecpar->codec_id));
        }
    }

    if (audio_stream_index != -1) {
        stream = fmt->streams[audio_stream_index];
        (*mi)->audio_bitrate_kbps = stream->codecpar->bit_rate / 1000;
        (*mi)->sample_rate = stream->codecpar->sample_rate;
        (*mi)->audio_channels = stream->codecpar->ch_layout.nb_channels;
        (*mi)->audio_codec = pkst_alloc(strlen(avcodec_get_name(stream->codecpar->codec_id)));
        if ((*mi)->audio_codec != NULL) {
            strcpy((*mi)->audio_codec, avcodec_get_name(stream->codecpar->codec_id));
        }
    }
    return 0;
}

int pktav_extract_mediainfo_from_file(const char *filename, TAVInfo **mi) {
    AVFormatContext *fmt;
    int ret;

    pktav_errno = 0;

    fmt = avformat_alloc_context();
    if (fmt == NULL) {
        pktav_errno = AVERROR(ENOMEM);
        return -AV_ERROR;
    }

    if ((ret = avformat_open_input(&fmt, filename, NULL, NULL)) != 0) {
        pktav_errno = ret;
        return -AV_ERROR;
    }

    if ((ret = avformat_find_stream_info(fmt, NULL)) < 0) {
        pktav_errno = ret;
        return -AV_ERROR;
    }

    ret = pkst_extract_mediainfo_from_avformat(fmt,mi);
    if (ret >= 0) {
        double duration = 0;
        int audio_pkts  = 0;
        int video_pkts  = 0;
        ret = pktav_count_packets(fmt, &duration, &audio_pkts, &video_pkts);
        if (ret >= 0) {
            (*mi)->audio_packets = audio_pkts;
            (*mi)->video_packets = video_pkts;
            (*mi)->duration = (*mi)->duration == -1 ? duration : (*mi)->duration;
        }
    } else {
        pktav_errno = ret;
        ret = -AV_ERROR;
    }

    avformat_close_input(&fmt);
    avformat_free_context(fmt);
    return ret;
}


void fprint_tavinfo(FILE *output, const TAVInfo *info) {
    fprintf(output,
        "Format: %s\n"
        "Duration: %.2f seconds\n"
        "Video Codec: %s\n"
        "Audio Codec: %s\n"
        "Video Index: %d\n"
        "Audio Index: %d\n"
        "Resolution: %dx%d\n"
        "Video Bitrate: %d kbps\n"
        "Audio Bitrate: %d kbps\n"
        "Frame Rate: %.2f fps\n"
        "Audio Channels: %d\n"
        "Sample Rate: %d Hz\n"
        "Audio Packets: %d\n"
        "Video Packets: %d\n",
        info->format, info->duration, info->video_codec, info->audio_codec,
        info->video_index, info->audio_index, info->width, info->height,
        info->video_bitrate_kbps, info->audio_bitrate_kbps, info->fps,
        info->audio_channels, info->sample_rate, info->audio_packets, info->video_packets
    );
}
