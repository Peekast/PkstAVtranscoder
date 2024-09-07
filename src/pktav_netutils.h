#ifndef _NETUTILS_H
#define _NETUTILS_H
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_SOCKET_FILE "unix.socket"

extern int unix_accept(int sd);
extern int unix_listener(const char *socket_path);
extern ssize_t recv_str(int socket, char *buffer, size_t max_len);
extern ssize_t send_str(int socket, const char *buffer);

#endif