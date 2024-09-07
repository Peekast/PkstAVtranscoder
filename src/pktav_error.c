#include <errno.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>


#define _PKTAV_ERROR_GLOBAL 1
int pktav_errno;
char *err_str[] = {
    "Success",
    "Video Stream not found",
    "Audio Stream not found",
    "Buffer too small to save the value",
    "Key not found",
};

#include "pktav_error.h"
char err_buff[ERR_BUFF_SIZE];

const char *pktav_strerror(int err) {
    switch (err)
    {
    case -OS_ERROR:
        return strerror(pktav_errno);
        break;
    
    case -PK_ERROR:
        return err_str[pktav_errno];

    case -AV_ERROR:
        memset(err_buff, 0, ERR_BUFF_SIZE);
        if (av_strerror(pktav_errno, err_buff, ERR_BUFF_SIZE) <0)
            strcpy(err_buff, "Unknow error from LIBAV");
        return err_buff;
    default:
        break;
    }
    return NULL;
}

