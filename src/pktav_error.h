#ifndef _PKTAV_ERROR_H
#define _PKTAV_ERROR_H 1

#define OS_ERROR 1
#define AV_ERROR 2
#define PK_ERROR 3

#define ERR_BUFF_SIZE 2048

#ifndef _PKTAV_ERROR_GLOBAL
    extern int pktav_errno;
    extern char err_buff[ERR_BUFF_SIZE];
    extern char *err_str[];
#endif

extern const char *pktav_strerror(int err);

#define PK_ERROR_VNOTFOUND   1
#define PK_ERROR_ANOTFOUND   2
#define PK_ERROR_BUFFTOSMALL 3
#define PK_ERROR_KEYNOTFOUND 4

#include <errno.h>

#endif 