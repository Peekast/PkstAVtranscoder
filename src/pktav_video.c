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


/**
 * @brief Initialize a TAVContext structure.
 * 
 * This function initializes a TAVContext structure by setting its fields to default values, including pointers 
 * to codec contexts, input and output streams, frames, and other necessary resources.
 *
 * @param ctx Pointer to a TAVContext structure to be initialized. If the pointer is NULL, the function does nothing.
 * 
 * @note This function sets all pointers in the structure to NULL and the codec type to -1. 
 *       It prepares the structure for further setup or use in encoding/decoding processes.
 */
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

/**
 * @brief Close and free all resources in a TAVContext structure.
 * 
 * This function releases and frees all allocated resources in the provided TAVContext structure, 
 * including codec contexts, frames, audio FIFO buffer, resample and scaling contexts.
 * After freeing the resources, the TAVContext structure is re-initialized to its default state using init_TAVContext().
 *
 * @param tavc Pointer to the TAVContext structure to be closed and cleaned. If NULL, the function does nothing.
 * 
 * @note This function ensures that all allocated resources within the TAVContext are properly freed to avoid memory leaks.
 * @note After this function is called, the TAVContext is reset and can be safely reused if needed.
 */
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

/**
 * @brief Open and initialize the default transcoder for decoding and encoding.
 * 
 * This function initializes the decoder and encoder for a given input stream and audio configuration. 
 * It allocates necessary resources such as input frames, decoder contexts, and encoder contexts, 
 * and sets up the transcoder for processing media streams.
 *
 * @param stream Pointer to an AVStream containing the codec parameters for the input stream.
 * @param config Pointer to a TAVConfigAudio structure that holds the configuration for the audio encoder.
 * @param tavc Pointer to the TAVContext structure where the transcoder's state will be stored.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 *         - AVERROR(ENOMEM) if memory allocation fails.
 *         - AVERROR_DECODER_NOT_FOUND if the decoder is not found.
 *         - AVERROR_ENCODER_NOT_FOUND if the encoder is not found.
 * 
 * @note The function allocates the input frame and codec contexts. In case of failure, it properly cleans up the allocated resources.
 * @note If any error occurs during initialization, the function frees all allocated resources and returns the corresponding error code.
 */
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

/**
 * @brief Configure the audio encoder based on the provided audio configuration and context.
 * 
 * This function sets up the audio encoder in the TAVContext using the configuration specified in 
 * the TAVConfigAudio structure. It configures the channel layout, sample rate, sample format, 
 * bitrate, and time base of the encoder.
 *
 * @param config Pointer to a TAVConfigAudio structure that contains the desired audio encoder configuration.
 * @param tavc Pointer to the TAVContext structure where the encoder context will be configured.
 * 
 * @return Returns 0 on success or a negative AVERROR code if the encoder cannot be opened.
 * 
 * @note The function assumes that the decoder context has already been initialized, and it copies relevant properties 
 *       (such as channel layout and sample rate) from the decoder to the encoder.
 * @note The encoder is opened with `avcodec_open2` and the default sample format is taken from the first format 
 *       available in the decoder's sample format list.
 */
static int pktav_config_audio_encoder(TAVConfigAudio *config, TAVContext *tavc) {
    av_channel_layout_default(&(tavc->encode_ctx->ch_layout), tavc->decode_ctx->ch_layout.nb_channels);
    tavc->encode_ctx->sample_rate    = tavc->decode_ctx->sample_rate;
    tavc->encode_ctx->sample_fmt     = tavc->decode_codec->sample_fmts[0];
    tavc->encode_ctx->bit_rate       = config->bitrate_bps;
    tavc->encode_ctx->time_base      = (AVRational){1, tavc->encode_ctx->sample_rate};
    tavc->encode_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    return avcodec_open2(tavc->encode_ctx, tavc->encode_codec, NULL);
}

/**
 * @brief Configure the video encoder based on the provided video configuration and context.
 * 
 * This function sets up the video encoder in the TAVContext using the settings specified in the 
 * TAVConfigVideo structure. It configures the encoder's resolution, GOP size, framerate, pixel format, 
 * and bitrate. The function also handles the selection between CRF or CBR modes based on the configuration.
 *
 * @param config Pointer to a TAVConfigVideo structure containing the desired video encoder configuration.
 * @param tavc Pointer to the TAVContext structure where the video encoder context will be configured.
 * 
 * @return Returns 0 on success or a negative AVERROR code if any configuration or allocation fails.
 *         - AVERROR(EINVAL) if invalid parameters are provided to the encoder options or scaling context.
 *         - AVERROR(ENOMEM) if memory allocation for scaling or frames fails.
 * 
 * @note The function configures the encoder to use either CRF (if `config->crf` is set) or a fixed bitrate for CBR.
 * @note If the decoder resolution is larger than the encoder's target resolution, a scaling context (SWS) is created.
 * @note On failure, the function ensures that any allocated resources (like scaling context or frames) are properly freed.
 */

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

/**
 * @brief Open and configure a transcoder for either audio or video streams.
 * 
 * This function initializes the transcoder by first opening the default transcoder for the provided input stream 
 * and then configuring the appropriate encoder (audio or video) based on the stream's codec type.
 *
 * @param stream Pointer to an AVStream containing the codec parameters for the input stream.
 * @param config Pointer to either a TAVConfigAudio or TAVConfigVideo structure for configuring the encoder.
 * @param tavc Pointer to the TAVContext structure that holds the transcoder's state and contexts.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 *         - The error could be from the default transcoder opening or from the encoder configuration.
 * 
 * @note If the input stream is audio, it configures the audio encoder; if it is video, it configures the video encoder.
 * @note On failure, the function ensures that all allocated resources are properly cleaned up by calling pktav_close_transcoder.
 */
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

/**
 * @brief Send a video packet to the decoder and process the resulting frames for encoding.
 * 
 * This function sends a compressed video packet to the decoder, receives the decoded frames, and either scales 
 * the frame (if necessary) or sends it directly to the video encoder. The scaling is performed if the output 
 * resolution is smaller than the input resolution, using the software scaling context (SWS).
 *
 * @param tavc Pointer to the TAVContext structure that holds the decoder, encoder, and frame contexts.
 * @param packet Pointer to the AVPacket that contains the compressed video data to be decoded.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 *         - AVERROR(EAGAIN) indicates that the encoder cannot accept new frames yet.
 *         - AVERROR_EOF indicates that the decoder has finished processing the stream.
 *         - Any other negative AVERROR code signals a failure in decoding, scaling, or encoding.
 * 
 * @note If scaling is required (i.e., if a scaling context is present), the function scales the decoded frame 
 *       before sending it to the encoder. If no scaling is required, the decoded frame is sent directly.
 * @note This function handles both the decoding and encoding steps and ensures that frames are unreferenced 
 *       after being processed to free their resources.
 */
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

/**
 * @brief Send an audio packet to the decoder and process the resulting frames for encoding.
 * 
 * This function sends a compressed audio packet to the decoder, receives the decoded audio frames, 
 * and sends them to the audio encoder. If resampling is needed, the function can be extended to 
 * handle audio resampling before encoding.
 *
 * @param tavc Pointer to the TAVContext structure that holds the decoder, encoder, and frame contexts.
 * @param packet Pointer to the AVPacket that contains the compressed audio data to be decoded.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 *         - AVERROR(EAGAIN) indicates that the encoder cannot accept new frames yet.
 *         - AVERROR_EOF indicates that the decoder has finished processing the stream.
 *         - Any other negative AVERROR code signals a failure in decoding or encoding.
 * 
 * @note The function currently sends decoded audio frames directly to the encoder. It includes 
 *       a placeholder for resampling logic, which can be implemented if needed for format or rate conversions.
 * @note After sending frames to the encoder, the function unreferences the input frame to free 
 *       its resources and prepare it for the next packet.
 */
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


/**
 * @brief Rescale video packet timestamps and adjust its duration.
 * 
 * This function adjusts the timestamps of a video packet to match the time base of the output stream. 
 * It also recalculates the packet duration based on the input and output stream time bases and frame rates.
 *
 * @param input Pointer to the AVStream representing the input stream from which the packet originates.
 * @param output Pointer to the AVStream representing the output stream to which the packet will be sent.
 * @param packet Pointer to the AVPacket representing the video packet whose timestamps are to be rescaled.
 * 
 * @note The function sets the packet's stream index to VIDEO_INDEX and calculates the packet's duration using 
 *       the time bases of the input and output streams, as well as the input stream's average frame rate.
 * @note After adjusting the duration, the function uses `av_packet_rescale_ts` to rescale the packet's 
 *       presentation and decoding timestamps to match the output stream's time base.
 */
static void pktav_rescale_video_packet(AVStream *input, AVStream *output, AVPacket *packet) {
    packet->stream_index = VIDEO_INDEX;
    packet->duration = input->time_base.den / output->time_base.num / input->avg_frame_rate.num * input->avg_frame_rate.den;
    av_packet_rescale_ts(packet, input->time_base, output->time_base);
}

/**
 * @brief Rescale audio packet timestamps.
 * 
 * This function adjusts the timestamps of an audio packet to match the time base of the output stream.
 *
 * @param input Pointer to the AVStream representing the input stream from which the packet originates.
 * @param output Pointer to the AVStream representing the output stream to which the packet will be sent.
 * @param packet Pointer to the AVPacket representing the audio packet whose timestamps are to be rescaled.
 * 
 * @note The function sets the packet's stream index to AUDIO_INDEX and uses `av_packet_rescale_ts` to adjust 
 *       the packet's presentation and decoding timestamps according to
 */
static void pktav_rescale_audio_packet(AVStream *input, AVStream *output, AVPacket *packet) {
    packet->stream_index = AUDIO_INDEX;
    av_packet_rescale_ts(packet, input->time_base, output->time_base);
}


/**
 * @brief Receive a video packet from the encoder and rescale its timestamps.
 * 
 * This function receives an encoded video packet from the encoder context in the TAVContext. 
 * Once the packet is successfully received, its timestamps are rescaled to match the time base of the output stream.
 *
 * @param tavc Pointer to the TAVContext structure that holds the encoding context and stream information.
 * @param packet Pointer to the AVPacket where the encoded video data will be stored.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 *         - AVERROR(EAGAIN) if the encoder needs more frames before producing a packet.
 *         - AVERROR_EOF if the encoder has finished encoding the stream.
 *         - AVERROR_INVALIDDATA if the TAVContext does not represent a video stream.
 * 
 * @note The function checks that the TAVContext is handling a video stream before attempting to receive a packet.
 * @note On success, the packet's timestamps are rescaled using `pktav_rescale_video_packet` to match the output stream's time base.
 */
int pktav_recv_video_packet(TAVContext *tavc, AVPacket *packet) {
    int error;
    if (tavc->codec_type != AVMEDIA_TYPE_VIDEO) 
        return AVERROR_INVALIDDATA;
    
    error = avcodec_receive_packet(tavc->encode_ctx, packet);
    if (error == 0) 
        pktav_rescale_video_packet(tavc->input_stream, tavc->output_stream, packet);

    return error;
}

/**
 * @brief Receive an audio packet from the encoder and rescale its timestamps.
 * 
 * This function receives an encoded audio packet from the encoder context in the TAVContext. 
 * Once the packet is successfully received, its timestamps are rescaled to match the time base of the output stream.
 *
 * @param tavc Pointer to the TAVContext structure that holds the encoding context and stream information.
 * @param packet Pointer to the AVPacket where the encoded audio data will be stored.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 *         - AVERROR(EAGAIN) if the encoder needs more frames before producing a packet.
 *         - AVERROR_EOF if the encoder has finished encoding the stream.
 *         - AVERROR_INVALIDDATA if the TAVContext does not represent an audio stream.
 * 
 * @note The function checks that the TAVContext is handling an audio stream before attempting to receive a packet.
 * @note On success, the packet's timestamps are rescaled using `pktav_rescale_audio_packet` to match the output stream's time base.
 */
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

static long current_time_ms() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec * 1000 + spec.tv_nsec / 1e6;
}

/**
 * @brief Process and transcode an input media stream and send progress updates to the client.
 * 
 * This function handles the complete workflow of reading, transcoding, and writing audio and video streams 
 * from an input format context to an output format context. It also calculates and sends progress updates 
 * to a client over a socket as the transcoding progresses.
 *
 * @param socket The socket descriptor used to send status updates to the client.
 * @param input The input media file or stream to be transcoded.
 * @param mi Pointer to a TAVInfo structure containing metadata about the input media (e.g., packet counts).
 * @param config_fmt Pointer to a TAVConfigFormat structure for configuring the output format.
 * @param config_audio Pointer to a TAVConfigAudio structure for configuring the audio transcoder.
 * @param config_video Pointer to a TAVConfigVideo structure for configuring the video transcoder.
 * 
 * @return Returns 0 on success, or a negative AVERROR code on failure.
 * 
 * @note The function initializes both the audio and video transcoders, processes each packet, and writes 
 *       the transcoded data to the output context. It also calculates the current progress and sends 
 *       periodic status updates to the client.
 * @note In case of failure, the function ensures proper cleanup of all allocated resources, including 
 *       packet memory, format contexts, and transcoder contexts.
 */
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
