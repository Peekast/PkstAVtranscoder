#include "pktav_proto.h"
#include "pktav_keyvalue.h"
#include "pktav_mediainfo.h"
#include "pktav_netutils.h"
#include "pktav_types.h"
#include "pktav_error.h"

static void pktav_config_kv_load(KeyValueList *kv_list, TAVConfigFormat *format_config, TAVConfigVideo *video_config, TAVConfigAudio *audio_config) {
    const char *value;

    // Audio configuration
    value = get_value_from_kv_list(kv_list, "audio_codec");
    if (value) audio_config->codec = strdup(value);

    value = get_value_from_kv_list(kv_list, "audio_bitrate_bps");
    if (value) audio_config->bitrate_bps = atoi(value);

    value = get_value_from_kv_list(kv_list, "audio_channels");
    if (value) audio_config->channels = atoi(value);

    value = get_value_from_kv_list(kv_list, "audio_sample_rate");
    if (value) audio_config->sample_rate = atoi(value);

    // Video configuration
    value = get_value_from_kv_list(kv_list, "video_codec");
    if (value) video_config->codec = strdup(value);

    value = get_value_from_kv_list(kv_list, "video_width");
    if (value) video_config->width = atoi(value);

    value = get_value_from_kv_list(kv_list, "video_height");
    if (value) video_config->height = atoi(value);

    value = get_value_from_kv_list(kv_list, "video_gop_size");
    if (value) video_config->gop_size = atoi(value);

    value = get_value_from_kv_list(kv_list, "video_pix_fmt");
    if (value) video_config->pix_fmt = atoi(value);

    value = get_value_from_kv_list(kv_list, "video_profile");
    if (value) video_config->profile = strdup(value);

    value = get_value_from_kv_list(kv_list, "video_preset");
    if (value) video_config->preset = strdup(value);

    value = get_value_from_kv_list(kv_list, "video_crf");
    if (value) video_config->crf = atoi(value);

    value = get_value_from_kv_list(kv_list, "video_bitrate_bps");
    if (value) video_config->bitrate_bps = atoi(value);

    // Format configuration
    value = get_value_from_kv_list(kv_list, "format_dst");
    if (value) format_config->dst = strdup(value);

    value = get_value_from_kv_list(kv_list, "format_dst_type");
    if (value) format_config->dst_type = strdup(value);

    value = get_value_from_kv_list(kv_list, "format_kv_opts");
    if (value) format_config->kv_opts = strdup(value);
}


static void pktav_mediainfo_kv_dump(KeyValueList **kv_list, TAVInfo *info) {
    char buffer[MAX_BUFFER_SIZE]; 
    
    if (info->format) {
        add_to_kv_list(kv_list, "format", info->format);
    }
    snprintf(buffer, sizeof(buffer), "%f", info->duration);
    add_to_kv_list(kv_list, "duration", buffer);
    
    if (info->video_codec) {
        add_to_kv_list(kv_list, "video_codec", info->video_codec);
    }
    if (info->audio_codec) {
        add_to_kv_list(kv_list, "audio_codec", info->audio_codec);
    }

    snprintf(buffer, sizeof(buffer), "%d", info->video_index);
    add_to_kv_list(kv_list, "video_index", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->audio_index);
    add_to_kv_list(kv_list, "audio_index", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->width);
    add_to_kv_list(kv_list, "width", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->height);
    add_to_kv_list(kv_list, "height", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->video_bitrate_kbps);
    add_to_kv_list(kv_list, "video_bitrate_kbps", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->audio_bitrate_kbps);
    add_to_kv_list(kv_list, "audio_bitrate_kbps", buffer);

    snprintf(buffer, sizeof(buffer), "%f", info->fps);
    add_to_kv_list(kv_list, "fps", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->audio_channels);
    add_to_kv_list(kv_list, "audio_channels", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->sample_rate);
    add_to_kv_list(kv_list, "sample_rate", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->audio_packets);
    add_to_kv_list(kv_list, "audio_packets", buffer);

    snprintf(buffer, sizeof(buffer), "%d", info->video_packets);
    add_to_kv_list(kv_list, "video_packets", buffer);
}

static void pktav_status_kv_dump(KeyValueList **kv_list, const TAVStatus *status) {
    char buffer[MAX_BUFFER_SIZE];

    // status
    snprintf(buffer, sizeof(buffer), "%d", status->status);
    add_to_kv_list(kv_list, "status", buffer);

    // status_desc
    add_to_kv_list(kv_list, "status_desc", status->status_desc);

    // proc_time_ms
    snprintf(buffer, sizeof(buffer), "%ld", status->proc_time_ms);
    add_to_kv_list(kv_list, "proc_time_ms", buffer);

    // time_left_ms
    snprintf(buffer, sizeof(buffer), "%ld", status->time_left_ms);
    add_to_kv_list(kv_list, "time_left_ms", buffer);

    // progress_pct
    snprintf(buffer, sizeof(buffer), "%d", status->progress_pct);
    add_to_kv_list(kv_list, "progress_pct", buffer);

    // audio_pkts_read
    snprintf(buffer, sizeof(buffer), "%d", status->audio_pkts_read);
    add_to_kv_list(kv_list, "audio_pkts_read", buffer);

    // video_pkts_read
    snprintf(buffer, sizeof(buffer), "%d", status->video_pkts_read);
    add_to_kv_list(kv_list, "video_pkts_read", buffer);

    // err_msg
    add_to_kv_list(kv_list, "err_msg", status->err_msg);
}

int send_mediainfo(int socket, TAVInfo *info) {
    int ret;
    char *str;
    KeyValueList *kv = NULL;
    pktav_errno = 0;

    pktav_mediainfo_kv_dump(&kv, info);

    str = kv_list_tostring(kv, PROTO_PAIRKV_DELIM, PROTO_KEYVAL_DELIM);
    if (!str) {
        pktav_errno = errno;
        free_kv_list(kv);
        return -OS_ERROR;
    }
    ret = send_str(socket, str);
    free_kv_list(kv);
    free(str);
    if (ret < 0) {
        pktav_errno = errno;
        ret = -OS_ERROR;
    }
    return ret;
}

int send_status(int socket, TAVStatus *status) {
    int ret;
    char *str;
    KeyValueList *kv = NULL;
    pktav_errno = 0;

    pktav_status_kv_dump(&kv, status);

    str = kv_list_tostring(kv, PROTO_PAIRKV_DELIM, PROTO_KEYVAL_DELIM);
    if (!str) {
        pktav_errno = errno;
        free_kv_list(kv);
        return -OS_ERROR;
    }
    ret = send_str(socket, str);
    free_kv_list(kv);
    free(str);
    if (ret < 0) {
        pktav_errno = errno;
        ret = -OS_ERROR;
    }
    return ret;
}

int recv_config(int socket, TAVConfigFormat *format, TAVConfigVideo *video, TAVConfigAudio *audio) {
    KeyValueList *kv = NULL;
    char buffer[MAX_BUFFER_SIZE];
    pktav_errno = 0;

    if (recv_str(socket, buffer, MAX_BUFFER_SIZE) < 0) {
        pktav_errno = errno;
        return -OS_ERROR;
    }
    kv = kv_list_fromstring(buffer,PROTO_PAIRKV_DELIM, PROTO_KEYVAL_DELIM);
    if (!kv) {
        pktav_errno = errno;
        return -OS_ERROR;
    }
    
    pktav_config_kv_load(kv, format, video, audio);

    free_kv_list(kv);
    return 0;
}

int recv_input(int socket, char *file, size_t len) {
    int ret = 0;
    KeyValueList *kv = NULL;
    char *tmp;
    char buffer[MAX_BUFFER_SIZE];

    pktav_errno = 0;

    if (recv_str(socket, buffer, MAX_BUFFER_SIZE) < 0) {
        pktav_errno = errno;
        return -OS_ERROR;
    }

    kv = kv_list_fromstring(buffer,PROTO_PAIRKV_DELIM, PROTO_KEYVAL_DELIM);
    if (!kv) {
        pktav_errno = errno;
        return -OS_ERROR;
    }
    tmp = (char *)get_value_from_kv_list(kv,INPUT_FILE_KEY);
    if (tmp) {
        if (strlen(tmp) < len) 
            strcpy(file, tmp);
        else {
            pktav_errno = PK_ERROR_BUFFTOSMALL;
            return -PK_ERROR;
        }
    } else {
        pktav_errno = PK_ERROR_KEYNOTFOUND;
        ret = -PK_ERROR;
    }

    free_kv_list(kv);
    return ret;
}

int send_error(int socket, const char *error) {
    char buffer[MAX_BUFFER_SIZE];
    sprintf(buffer, "error%c%s", PROTO_KEYVAL_DELIM, error);
    return send_str(socket, buffer);
}