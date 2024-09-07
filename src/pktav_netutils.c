#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pktav_error.h"

int unix_listener(const char *socket_path) {
    int server_sock;
    struct sockaddr_un addr;

    pktav_errno = 0;

    if ((server_sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        pktav_errno = errno;
        return -OS_ERROR;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);


    unlink(socket_path);


    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
        close(server_sock);
        pktav_errno = errno;
        return -OS_ERROR;
    }

    if (listen(server_sock, 5) < 0) {
        close(server_sock);
        pktav_errno = errno;
        return -OS_ERROR;
    }

    return server_sock; 
}

int unix_accept(int sd) {
    int ret;

    pktav_errno = 0;

    while((ret = accept(sd, NULL, NULL)) < 0 && errno == EINTR);
    if (ret < 0) {
        pktav_errno = errno;
        ret = -OS_ERROR;
    }
    return ret;
}

ssize_t recv_str(int socket, char *buffer, size_t max_len) {
    ssize_t total_bytes_read = 0;
    ssize_t bytes_read;

    while (total_bytes_read < max_len - 1) {
        bytes_read = read(socket, buffer + total_bytes_read, max_len - total_bytes_read - 1);
        if (bytes_read <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1; 
        }
        
        total_bytes_read += bytes_read;
        if (buffer[total_bytes_read - 1] == '\0') {
            break;
        }
    }
    return total_bytes_read;
}

ssize_t send_str(int socket, const char *buffer) {
    size_t total_bytes_written = 0;
    size_t len = strlen(buffer) + 1;

    while (total_bytes_written < len) {
        ssize_t bytes_written = write(socket, buffer + total_bytes_written, len - total_bytes_written);
        
        if (bytes_written <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1; 
        }

        total_bytes_written += bytes_written;
    }

    return total_bytes_written;
}