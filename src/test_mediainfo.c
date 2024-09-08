#include <unistd.h>
#include <limits.h>
#include "pktav_netutils.h"
#include "pktav_proto.h"
#include "pktav_video.h"
#include "pktav_mediainfo.h"
#include "pktav_sigchld.h"
#include "pktav_error.h"
#include "pktav_log.h"
#include "pktav_version.h"

int main(int argc, char *argv[]) {
    int socket;
    int err;
    char *socket_file = getenv("UNIX_SOCKET");
    char input_file[PATH_MAX+1];
    TAVConfigFormat format;
    TAVConfigVideo  video; 
    TAVConfigAudio  audio;
    TAVInfo *mi = NULL;
    
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        fprintf(stdout, "%s\n", VERSION);
        exit(EXIT_SUCCESS);
    }

    fprintf(stdout, "PkstAVTranscoder version %s Copyright (c) 2022-2024 Peekast Media LLC\nbuild with: %s at: %s-%s\n", VERSION, COMPILER, DATE, TIME);

    /*
     * Register the log callback for libav
     */
    av_log_set_callback(pktav_log_callback);
    
    if (socket_file == NULL) 
        socket_file = DEFAULT_SOCKET_FILE;
    
    /*
     * Unix socket (pktav_netutils.c)
     */
    socket = unix_listener(socket_file);
    if (socket < 0) {
        pktav_log(NULL, 0, "Error unix_listener(%s): %s, return: %d\n", socket_file, pktav_strerror(socket), socket);
        exit(EXIT_FAILURE);
    }
    /*
     * Register the SIGCHLD Handler (pktav_sigchld.c)
     */
    err = set_sigchld_handler();
    if (err != 0) {
        pktav_log(NULL, 0, "Error set_sigchld_handler(): %s, return: %d\n", pktav_strerror(err), err);
        exit(EXIT_FAILURE);
    }

    while(1) {
        pid_t wpid; /* Worker PID */
        int client;
        /*
         * Ready to start accepting new connections
         */
        client = unix_accept(socket);
        if (client < 0) {
            pktav_log(NULL, 0, "Error unix_accept(): %s, return: %d\n", pktav_strerror(client), client);
            /* Why? continue or exit ... that is the question */
            continue;
        }
        pktav_log(NULL, 0, "New connection\n");
        /*
         * Now the server is ready to fork a new worker to process the next job
         */
        wpid = 0; /*fork(); */
        switch (wpid)
        {
        case -1: /* Error */
            pktav_log(NULL, 0, "Error at fork(): %s, return: %d\n", strerror((int)wpid), (int)wpid);
            close(client);
            close(socket);
            exit(EXIT_SUCCESS);
            
        case 0:  /* In the Child Process  */
            close(socket);

            err = recv_input(client, input_file, PATH_MAX+1);
            if (err < 0) {
                pktav_log(NULL, 0, "Error recv_input: %s, return: %d - End process -\n", pktav_strerror(err), err);
                close(client);
                exit(EXIT_FAILURE);
            }

            pktav_log(NULL, 0, "Extracting media information from file: %s\n", input_file);

            err = pktav_extract_mediainfo_from_file((const char *) input_file, &mi);
            if (err < 0) {
                pktav_log(NULL, 0, "Error extracting media information from file(%s): %s, return: %d - End process -\n", 
                                   input_file, pktav_strerror(err), err);
                send_error(client, pktav_strerror(err));
                close(client);
                exit(EXIT_FAILURE);
            }

            pktav_log(NULL, 0, "Result(%s): format: %s, resolution: %dx%d, vcodec: %s, acodec: %s, vbitrate: %dkbps, abitrate: %dkbps\n", 
                                input_file,
                                mi->format, 
                                mi->width, 
                                mi->height, 
                                mi->video_codec, 
                                mi->audio_codec, 
                                mi->video_bitrate_bps, 
                                mi->audio_bitrate_bps);

            err = send_mediainfo(client, mi);
            if (err < 0) {
                pktav_log(NULL, 0, "Error sending media information: %s, return: %d - End process -\n", pktav_strerror(err), err);
                close(client);
                exit(EXIT_FAILURE);
            }

            err = recv_config(client, &format, &video, &audio);
            if (err < 0) {
                pktav_log(NULL, 0, "Error reciving configuration: %s, return: %d - End process -\n", pktav_strerror(err), err);
                close(client);
                exit(EXIT_FAILURE);
            }

            dump_TAVConfigFormat(&format);
            dump_TAVConfigVideo(&video);
            dump_TAVConfigAudio(&audio);

            err = pktav_worker(client, input_file, mi, &format, &audio, &video);
            if (err < 0) {
                TAVStatus status;
                memset(&status, 0, sizeof(TAVStatus));
                pktav_log(NULL, 0, "Worker fail: %s, return: %d - End process -\n", pktav_strerror(err), err);
                status.err_msg = (char *)pktav_strerror(err);
                status.status  = -1;
                status.status_desc = "FAILED";
                send_status(client, &status);
            }
            pktav_log(NULL, 0, "Worker finish - End process -\n");
            /* TODO: Limpiar Mediainfo struct */
            exit(EXIT_SUCCESS);
            break;

        default: /* In the Parent Process */
            close(client);
        }
    }
}
