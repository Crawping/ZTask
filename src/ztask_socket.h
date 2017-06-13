#ifndef _ZTASK_SOCKET_H_
#define _ZTASK_SOCKET_H_
#include "ztask.h"
struct ztask_context;

#define ZTASK_SOCKET_TYPE_DATA 1
#define ZTASK_SOCKET_TYPE_CONNECT 2
#define ZTASK_SOCKET_TYPE_CLOSE 3
#define ZTASK_SOCKET_TYPE_ACCEPT 4
#define ZTASK_SOCKET_TYPE_ERROR 5
#define ZTASK_SOCKET_TYPE_UDP 6
#define ZTASK_SOCKET_TYPE_WARNING 7

struct ztask_socket_message {
    int type;
    int id;
    int ud;
    char * buffer;
};

void ztask_socket_init();
void ztask_socket_exit();
void ztask_socket_free();
int ztask_socket_poll();

#endif
