#ifndef _PKTAV_PROTO_H 
#define _PKTAV_PROTO_H 1

#define PROTO_PAIRKV_DELIM ';'
#define PROTO_KEYVAL_DELIM ':'

#define MAX_BUFFER_SIZE 4096
#define INPUT_FILE_KEY "input_file"

#include "pktav_mediainfo.h"
#include "pktav_types.h"

extern int send_mediainfo(int socket, TAVInfo *info);
extern int send_status(int socket, TAVStatus *status);
extern int recv_config(int socket, TAVConfigFormat *format, TAVConfigVideo *video, TAVConfigAudio *audio);
extern int recv_input(int socket, char *file, size_t len);
extern int send_error(int socket, const char *error);

#endif