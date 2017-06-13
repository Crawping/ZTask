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
#define ZTASK_SOCKET_TYPE_BIND 8

struct ztask_socket_message {
    int type;
    int id;
    int ud;
    char * buffer;
};
ZTASK_EXTERN int ztask_socket_send(struct ztask_context *ctx, int id, void *buffer, int sz);
ZTASK_EXTERN int ztask_socket_send_lowpriority(struct ztask_context *ctx, int id, void *buffer, int sz);
ZTASK_EXTERN int ztask_socket_listen(struct ztask_context *ctx, int session, const char *host, int port, int backlog);
ZTASK_EXTERN int ztask_socket_connect(struct ztask_context *ctx, int session, const char *host, int port);
ZTASK_EXTERN int ztask_socket_bind(struct ztask_context *ctx, int fd);
ZTASK_EXTERN void ztask_socket_close(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_shutdown(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_start(struct ztask_context *ctx, int session, int id);
ZTASK_EXTERN void ztask_socket_nodelay(struct ztask_context *ctx, int id);

ZTASK_EXTERN int ztask_socket_udp(struct ztask_context *ctx, const char * addr, int port);
ZTASK_EXTERN int ztask_socket_udp_connect(struct ztask_context *ctx, int id, const char * addr, int port);
ZTASK_EXTERN int ztask_socket_udp_send(struct ztask_context *ctx, int id, const char * address, const void *buffer, int sz);
ZTASK_EXTERN const char * ztask_socket_udp_address(struct ztask_socket_message *, int *addrsz);
void ztask_socket_init();
void ztask_socket_exit();
void ztask_socket_free();
int ztask_socket_poll();

#endif
