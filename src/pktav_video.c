#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include "pktav_mediainfo.h"
#include "pktav_keyvalue.h"
#include "pktav_error.h"
#include "pktav_types.h"
#include "pktav_proto.h"

#define PKST_PAIR_DELIM '&'
#define PKST_KV_DELIM   '='

#define HANDLER_NAME "Media file produced by Peekast Media LLC (2024)."
#define VIDEO_INDEX 0
#define AUDIO_INDEX 1
#define DEFAULT_PIX_FMT AV_PIX_FMT_YUV420P


void init_TAVContext(TAVContext *ctx) {
    if (!ctx) return;
    ctx->codec_type = -1;
    ctx->input_stream = NULL;
    ctx->output_stream = NULL;
    ctx->decode_codec = NULL;
    ctx->encode_codec = NULL;
    ctx->decode_ctx = NULL;
    ctx->encode_ctx = NULL;
    ctx->input_frame = NULL;
    ctx->scale_frame = NULL;
    ctx->fifo = NULL;
    ctx->resample_ctx = NULL;
    ctx->sws_ctx = NULL;
}

void pktav_close_transcoder(TAVContext *tavc) {
    if (tavc == NULL) 
        return;

    // Free the decode context
    if (tavc->decode_ctx) avcodec_free_context(&(tavc->decode_ctx));

    // Free the encode context
    if (tavc->encode_ctx) avcodec_free_context(&(tavc->encode_ctx));
        
    // Free the input frame
    if (tavc->input_frame) av_frame_free(&(tavc->input_frame));

    // Free the scale frame
    if (tavc->scale_frame) av_frame_free(&(tavc->scale_frame));

    // Free the audio FIFO buffer
    if (tavc->fifo) av_audio_fifo_free(tavc->fifo);

    // Free the resample context
    if (tavc->resample_ctx) swr_free(&(tavc->resample_ctx));

    // Free the scaling context
    if (tavc->sws_ctx) sws_freeContext(tavc->sws_ctx);

    init_TAVContext(tavc);
}

int pktav_open_default_transcoder(AVStream *stream, TAVConfigAudio *config, TAVContext *tavc) {
    int error = 0;

    tavc->input_frame = av_frame_alloc();
    if (!tavc->input_frame) {
        return AVERROR(ENOMEM);
    }

    tavc->codec_type = stream->codecpar->codec_type;
    tavc->input_stream = stream;

    tavc->decode_codec = (AVCodec *)avcodec_find_decoder(stream->codecpar->codec_id);
    if (!tavc->decode_codec) {
        av_frame_free(&tavc->input_frame);
        return AVERROR_DECODER_NOT_FOUND;
    }

    tavc->decode_ctx = avcodec_alloc_context3(tavc->decode_codec);
    if (!tavc->decode_ctx) {
        av_frame_free(&tavc->input_frame);
        return AVERROR(ENOMEM);
    }

    /* Copia los parámetros del contexto */
    error = avcodec_parameters_to_context(tavc->decode_ctx, stream->codecpar);
    if (error < 0) {
        goto cleanup_decode_error;
    }

    /* Abre el decodificador para usarlo más tarde. */
    error = avcodec_open2(tavc->decode_ctx, tavc->decode_codec, NULL);
    if (error < 0) {
        goto cleanup_decode_error;
    }

    tavc->encode_codec = (AVCodec *)avcodec_find_encoder_by_name((const char *)config->codec);
    if (!tavc->encode_codec) {
        error = AVERROR_ENCODER_NOT_FOUND;
        goto cleanup_decode_error;
    }
        
    tavc->encode_ctx = avcodec_alloc_context3(tavc->encode_codec);
    if (!tavc->encode_ctx) {
        error = AVERROR(ENOMEM);
        goto cleanup_decode_error;
    }

    return 0;

cleanup_decode_error:
    avcodec_free_context(&(tavc->decode_ctx));
    av_frame_free(&tavc->input_frame);
    return error;
}

static int pktav_config_audio_encoder(TAVConfigAudio *config, TAVContext *tavc) {
    av_channel_layout_default(&(tavc->encode_ctx->ch_layout), tavc->decode_ctx->ch_layout.nb_channels);
    tavc->encode_ctx->sample_rate    = tavc->decode_ctx->sample_rate;
    tavc->encode_ctx->sample_fmt     = tavc->decode_codec->sample_fmts[0];
    tavc->encode_ctx->bit_rate       = config->bitrate_bps;
    tavc->encode_ctx->time_base      = (AVRational){1, tavc->encode_ctx->sample_rate};
    tavc->encode_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    return avcodec_open2(tavc->encode_ctx, tavc->encode_codec, NULL);
}

static int pktav_config_video_encoder(TAVConfigVideo *config, TAVContext *tavc) {
    tavc->encode_ctx->width = config->width;
    tavc->encode_ctx->height = config->height;
    tavc->encode_ctx->gop_size = config->gop_size;
    tavc->encode_ctx->time_base = av_inv_q(config->framerate);
    tavc->encode_ctx->sample_aspect_ratio = tavc->decode_ctx->sample_aspect_ratio;
    tavc->encode_ctx->pix_fmt = config->pix_fmt;

    if (config->crf != -1) {
        tavc->encode_ctx->bit_rate = 0;
        av_opt_set_int(tavc->encode_ctx->priv_data, "crf", config->crf, 0);
    } else {
        tavc->encode_ctx->bit_rate = config->bitrate_bps;
        tavc->encode_ctx->rc_buffer_size = 2 * config->bitrate_bps;
        tavc->encode_ctx->rc_max_rate = config->bitrate_bps;
        av_opt_set(tavc->encode_ctx->priv_data, "tune", "zerolatency", 0);
    }
    

    if (av_opt_set(tavc->encode_ctx->priv_data, "preset", config->preset, 0) < 0) {
        return AVERROR(EINVAL);
    }
    if (av_opt_set(tavc->encode_ctx->priv_data, "profile", config->profile, 0) < 0) {
        return AVERROR(EINVAL);
    }


    if (tavc->decode_ctx->width > tavc->encode_ctx->width && 
        tavc->decode_ctx->height > tavc->encode_ctx->height) {
        tavc->sws_ctx = sws_getContext(
                            tavc->decode_ctx->width, tavc->decode_ctx->height, tavc->decode_ctx->pix_fmt, 
                            tavc->encode_ctx->width, tavc->encode_ctx->height, tavc->encode_ctx->pix_fmt, 
                            SWS_BILINEAR, NULL, NULL, NULL);
        if (!tavc->sws_ctx)
            return AVERROR(EINVAL); // Invalid parameters

        tavc->scale_frame = av_frame_alloc();
        if (!tavc->scale_frame) {
            sws_freeContext(tavc->sws_ctx);
            return AVERROR(ENOMEM);
        }
    } else {
        tavc->sws_ctx = NULL;
        tavc->scale_frame = NULL;
    }
    return avcodec_open2(tavc->encode_ctx, tavc->encode_codec, NULL);
}


int pktav_open_transcoder(AVStream *stream, void *config, TAVContext *tavc) {
    int error;
    if ((error = pktav_open_default_transcoder(stream, config, tavc)) < 0) 
        return error; 
        
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        error = pktav_config_audio_encoder(config, tavc);
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        error = pktav_config_video_encoder(config, tavc);

    if (error < 0)
        pktav_close_transcoder(tavc);

    return error;
}

int pktav_send_video_packet(TAVContext *tavc, AVPacket *packet) {
    int error = 0;

    error = avcodec_send_packet(tavc->decode_ctx, packet);
    if (error < 0) {
        return error;
    }

    while (error >= 0) {
        error = avcodec_receive_frame(tavc->decode_ctx, tavc->input_frame);
        if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
            break; // No más cuadros disponibles o fin del flujo
        } else if (error < 0) {
            return error;
        }

        if (tavc->sws_ctx) {
            tavc->scale_frame->format = tavc->encode_ctx->pix_fmt;
            tavc->scale_frame->width  = tavc->encode_ctx->width;
            tavc->scale_frame->height = tavc->encode_ctx->height;
            av_frame_get_buffer(tavc->scale_frame, 32); 

            sws_scale(tavc->sws_ctx, (const uint8_t * const *)tavc->input_frame->data,
                      tavc->input_frame->linesize, 0, tavc->decode_ctx->height,
                      tavc->scale_frame->data, tavc->scale_frame->linesize);
            tavc->scale_frame->pts = tavc->input_frame->pts;

            error = avcodec_send_frame(tavc->encode_ctx, tavc->scale_frame);
            av_frame_unref(tavc->scale_frame);
        } else {
            error = avcodec_send_frame(tavc->encode_ctx, tavc->input_frame);
            av_frame_unref(tavc->input_frame);
        }

        if (error < 0) {
            return error;
        }
    }

    return error == AVERROR_EOF || error == AVERROR(EAGAIN) ? 0 : error;
}

int pktav_send_audio_packet(TAVContext *tavc, AVPacket *packet) {
    int error = 0;

    error = avcodec_send_packet(tavc->decode_ctx, packet);
    if (error < 0) {
        return error;
    }

    while (error >= 0) {
        error = avcodec_receive_frame(tavc->decode_ctx, tavc->input_frame);
        if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
            break; // No más cuadros disponibles o fin del flujo
        } else if (error < 0) {
            return error;
        }
        if (0) {
            /* RESAMPLER */
        } else {
            error = avcodec_send_frame(tavc->encode_ctx, tavc->input_frame);
            av_frame_unref(tavc->input_frame);
        }

        if (error < 0) {
            return error;
        }
    }

    return error == AVERROR_EOF || error == AVERROR(EAGAIN) ? 0 : error;
}



static void pktav_rescale_video_packet(AVStream *input, AVStream *output, AVPacket *packet) {
    packet->stream_index = VIDEO_INDEX;
    packet->duration = input->time_base.den / output->time_base.num / input->avg_frame_rate.num * input->avg_frame_rate.den;
    av_packet_rescale_ts(packet, input->time_base, output->time_base);
}

static void pktav_rescale_audio_packet(AVStream *input, AVStream *output, AVPacket *packet) {
    packet->stream_index = AUDIO_INDEX;
    av_packet_rescale_ts(packet, input->time_base, output->time_base);
}



int pktav_recv_video_packet(TAVContext *tavc, AVPacket *packet) {
    int error;
    if (tavc->codec_type != AVMEDIA_TYPE_VIDEO) 
        return AVERROR_INVALIDDATA;
    
    error = avcodec_receive_packet(tavc->encode_ctx, packet);
    if (error == 0) 
        pktav_rescale_video_packet(tavc->input_stream, tavc->output_stream, packet);

    return error;
}

int pktav_recv_audio_packet(TAVContext *tavc, AVPacket *packet) {
    int error;
    if (tavc->codec_type != AVMEDIA_TYPE_AUDIO) 
        return AVERROR_INVALIDDATA;
    
    error = avcodec_receive_packet(tavc->encode_ctx, packet);
    if (error == 0) 
        pktav_rescale_audio_packet(tavc->input_stream, tavc->output_stream, packet);

    return error;
}


int pktav_open_input_context(const char *input_media, AVFormatContext **avfc, AVDictionary *options) {
    int ret;
    *avfc = avformat_alloc_context();
    if (!*avfc) {
        return AVERROR(ENOMEM);
    }

    ret = avformat_open_input(avfc, input_media, NULL, options ? &options : NULL);
    if (ret < 0) {
        avformat_free_context(*avfc);
        return ret;
    }

    ret = avformat_find_stream_info(*avfc, NULL);
    if (ret < 0) {
        avformat_free_context(*avfc);
        return ret;
    }
    return 0;
}

int pktva_get_video_stream(AVFormatContext *avfc, AVStream **stream) {
    int i;
    for (i = 0; i < avfc->nb_streams; i++) {
        if (avfc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            *stream = avfc->streams[i];
            return i;
        }
    }
    return -1;
}

int pktva_get_audio_stream(AVFormatContext *avfc, AVStream **stream) {
    int i;
    for (i = 0; i < avfc->nb_streams; i++) {
        if (avfc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            *stream = avfc->streams[i];
            return i;
        }
    }
    return -1;
}

/**
 * @brief Initialize an output context for remuxing or encoding AV streams.
 * 
 * This function creates an AVFormatContext for output, based on the provided configuration 
 * and input AV streams. It handles both remuxing and encoding of video and audio streams.
 *
 * @param config Pointer to a PKSTOutputConfig structure containing the configuration for the output context.
 * @note At each index, either the remux or enc pointer should be non-null, but not both. If both are non-null, 
 *       the function returns NULL.
 * @note If the function fails at any point, it will free all resources it has allocated up to that point and return NULL.
 * @note If the function is successful, it returns a pointer to the created AVFormatContext. The caller is 
 *       responsible for freeing this with avformat_free_context().
 *
 * @return On success, a pointer to the created AVFormatContext. On failure, returns NULL.
 */

int pktva_open_output_context(TAVConfigFormat *config, AVFormatContext **ctx, TAVContext *video_enc, TAVContext *audio_enc) {
    int error, i;
    const AVOutputFormat *ofmt;
    AVDictionary *opts = NULL;
    KeyValueList *kv_opts;
    
    if (video_enc == NULL || audio_enc == NULL)
        return AVERROR_ENCODER_NOT_FOUND;


    error = avformat_alloc_output_context2(ctx, NULL, config->dst_type, config->dst);
    if (error <0) {
        return error;
    }
    ofmt = (*ctx)->oformat;

    if ((video_enc->output_stream = avformat_new_stream(*ctx, NULL)) == NULL) {
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    error = avcodec_parameters_from_context(video_enc->output_stream->codecpar, video_enc->encode_ctx);
    if (error < 0) goto cleanup;

    if (ofmt->flags & AVFMT_GLOBALHEADER)
        video_enc->encode_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_dict_set(&(video_enc->output_stream->metadata), "handler_name",HANDLER_NAME, 0);

    if ((audio_enc->output_stream = avformat_new_stream(*ctx, NULL)) == NULL) {
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    error = avcodec_parameters_from_context(audio_enc->output_stream->codecpar, audio_enc->encode_ctx);
    if (error < 0) goto cleanup;

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        error = avio_open(&((*ctx)->pb), config->dst, AVIO_FLAG_WRITE);
        if (error < 0)
            goto cleanup;
    }
    if (config->kv_opts) {
        kv_opts = kv_list_fromstring(config->kv_opts, PKST_PAIR_DELIM, PKST_KV_DELIM);
        if (kv_opts) {
            for (i=0; i < kv_opts->count; i++) 
                av_dict_set(&opts, kv_opts->items[i].key, kv_opts->items[i].value, 0);
            free_kv_list(kv_opts);
            kv_opts = NULL;
        }
    }

    if (opts) {
        error = avformat_write_header(*ctx, &opts);
        av_dict_free(&opts);
    } else {
        error = avformat_write_header(*ctx, NULL);
    }
    if (error < 0) 
        goto close;
    
    return 0;

close:
    if (*ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&((*ctx)->pb));
cleanup:
    if (*ctx)
        avformat_free_context(*ctx);
    *ctx = NULL;
    return error;
} 

long current_time_ms() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec * 1000 + spec.tv_nsec / 1e6;
}

int pktav_worker(int socket, const char *input, TAVInfo *mi, TAVConfigFormat *config_fmt, TAVConfigAudio *config_audio, TAVConfigVideo *config_video) {
    int error = 0;
    AVStream *saudio = NULL;
    AVStream *svideo = NULL;
    AVPacket *packet = NULL;
    AVFormatContext *ifc = NULL;    /* Input Format Context  */
    AVFormatContext *ofc = NULL;    /* Output Format Context */
    TAVContext tvideo;              /* Video Transcoder */
    TAVContext taudio;              /* Audio Transcoder */
    time_t start_time;
    int apkts = 0;
    int vpkts = 0;
    int current_pct = 0;
    int counter = 0;

    init_TAVContext(&tvideo);
    init_TAVContext(&taudio);
    /*
     * Open input context
     */
    error = pktav_open_input_context(input, &ifc, NULL);
    if (error < 0) {
        pktav_errno = error;
        return -AV_ERROR;
    }

    if (pktva_get_video_stream(ifc, &svideo) == -1) {
        /* Video stream not found - Why ? */
        pktav_errno = PK_ERROR_VNOTFOUND;
        return -PK_ERROR;
        goto cleanup_input;
    }

    if (pktva_get_audio_stream(ifc, &saudio) == -1) {
        /* Audio stream not found - Why ? */
        pktav_errno = PK_ERROR_ANOTFOUND;
        error = -PK_ERROR;
        goto cleanup_input;
    }

    config_video->framerate = av_guess_frame_rate(ifc, svideo, NULL);
    config_video->pix_fmt = DEFAULT_PIX_FMT;

    /*
     * Open the video transcoder
     */
    error = pktav_open_transcoder(svideo, config_video, &tvideo);
    if (error < 0) {
        pktav_errno = error;
        error = -AV_ERROR;
        goto cleanup_input;
    }

    /*
     * Open the audio transcoder
     */
    error = pktav_open_transcoder(saudio, config_audio, &taudio);
    if (error < 0) {
        pktav_errno = error;
        error = -AV_ERROR;
        goto cleanup_tvideo;
    }

    /* Open output context */
    error = pktva_open_output_context(config_fmt, &ofc, &tvideo, &taudio);
    if (error < 0) {
        pktav_errno = error;
        error = -AV_ERROR;
        goto cleanup_taudio;
    }

    /* Alloc the package to process A/V */
    if ((packet = av_packet_alloc()) == NULL) {
        pktav_errno = AVERROR(ENOMEM);
        error = -AV_ERROR;
        goto cleanup_output;
    }

    start_time = current_time_ms();
    while ((error = av_read_frame(ifc, packet)) == 0) {

        if (packet->stream_index == mi->video_index) {
            vpkts++;
            error = pktav_send_video_packet(&tvideo, packet);
            if (error < 0) {
                pktav_errno = error;
                error = -AV_ERROR;
                goto cleanup_packet;
            }
            av_packet_unref(packet);
            
            while (pktav_recv_video_packet(&tvideo, packet) == 0){
                av_interleaved_write_frame(ofc, packet);
                av_packet_unref(packet);
            }
        } else if (packet->stream_index == mi->audio_index) {
            apkts++;
            error = pktav_send_audio_packet(&taudio, packet);
            if (error < 0) {
                pktav_errno = error;
                error = -AV_ERROR;
                goto cleanup_packet;
            }
            av_packet_unref(packet);
            
            while (pktav_recv_audio_packet(&taudio, packet) == 0) {
                av_interleaved_write_frame(ofc, packet);
                av_packet_unref(packet);
            }
        }
        /*
         * Calculate the currect percentage
         */
        current_pct = ((apkts + vpkts) * 100) / (mi->video_packets + mi->audio_packets);
        if (current_pct > counter) {
            TAVStatus status;/* Update the status and send it to the client */
            counter = current_pct;
            status.audio_pkts_read = apkts;
            status.video_pkts_read = vpkts;
            status.proc_time_ms = current_time_ms() - start_time;
            status.progress_pct = current_pct;
            status.time_left_ms = (status.proc_time_ms * (100 - status.progress_pct)) / status.progress_pct;
            status.err_msg = "";
            status.status = 0;
            status.status_desc = "TRANSCODING";
            error = send_status(socket, &status);
            if ( error < 0) 
                goto cleanup_packet;
        }
    }
    error = av_write_trailer(ofc);
    if (error < 0) {
        pktav_errno = error;
        error = -AV_ERROR;
    } else {
        TAVStatus status;
        counter = current_pct;
        status.audio_pkts_read = apkts;
        status.video_pkts_read = vpkts;
        status.proc_time_ms = current_time_ms() - start_time;
        status.progress_pct = current_pct;
        status.time_left_ms = (status.proc_time_ms * (100 - status.progress_pct)) / status.progress_pct;
        status.err_msg = "";
        status.status = 1;
        status.status_desc = "FINISH";
        error = send_status(socket, &status);
    }

cleanup_packet:
    av_packet_free(&packet);
cleanup_output:
    avformat_close_input(&ofc);
    avformat_free_context(ofc);
cleanup_taudio:
    pktav_close_transcoder(&taudio);
cleanup_tvideo:
    pktav_close_transcoder(&tvideo);
cleanup_input:
    avformat_close_input(&ifc);
    avformat_free_context(ifc);
    return error;
}
