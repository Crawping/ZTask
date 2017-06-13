#include "ztask.h"

#include "ztask_socket.h"
#include "socket_server.h"
#include "ztask_server.h"
#include "ztask_mq.h"
#include "ztask_harbor.h"
#include "ztask_handle.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static struct socket_server * SOCKET_SERVER = NULL;
void forward_message(int type, bool padding, struct socket_message * result);
void socket_server_cb(int type, struct socket_message *result)
{
    switch (type) {
    case SOCKET_EXIT:
        return;
    case SOCKET_DATA:
        forward_message(ZTASK_SOCKET_TYPE_DATA, false, result);
        break;
    case SOCKET_CLOSE:
        forward_message(ZTASK_SOCKET_TYPE_CLOSE, false, result);
        break;
    case SOCKET_OPEN:
        forward_message(ZTASK_SOCKET_TYPE_CONNECT, true, result);
        break;
    case SOCKET_ERR:
        forward_message(ZTASK_SOCKET_TYPE_ERROR, false, result);
        break;
    case SOCKET_ACCEPT:
        forward_message(ZTASK_SOCKET_TYPE_ACCEPT, true, result);
        break;
    case SOCKET_UDP:
        forward_message(ZTASK_SOCKET_TYPE_UDP, false, result);
        break;
    case SOCKET_BIND:
        forward_message(ZTASK_SOCKET_TYPE_BIND, false, result);
        break;
    default:
        ztask_error(NULL, "Unknown socket message type %d.", type);
    }
}

void
ztask_socket_init() {
    SOCKET_SERVER = socket_server_create(&socket_server_cb);
}

void
ztask_socket_exit() {
    socket_server_exit(SOCKET_SERVER);
}

void
ztask_socket_free() {
    socket_server_release(SOCKET_SERVER);
    SOCKET_SERVER = NULL;
}

// mainloop thread
void
forward_message(int type, bool padding, struct socket_message * result) {
    struct ztask_socket_message *sm;
    size_t sz = sizeof(*sm);
    if (padding) {
        if (result->data) {
            size_t msg_sz = strlen(result->data);
            if (msg_sz > 128) {
                msg_sz = 128;
            }
            sz += msg_sz;
        }
        else {
            result->data = "";
        }
    }
    sm = (struct ztask_socket_message *)ztask_malloc(sz);
    sm->type = type;
    sm->id = result->id;
    sm->ud = result->ud;
    if (padding) {
        sm->buffer = NULL;
        memcpy(sm + 1, result->data, sz - sizeof(*sm));
    }
    else {
        sm->buffer = result->data;
    }

    struct ztask_message message;
    message.source = 0;
    if (type == ZTASK_SOCKET_TYPE_CONNECT || type == ZTASK_SOCKET_TYPE_BIND)
        message.session = result->session;
    else
        message.session = 0;
    message.data = sm;
    message.sz = sz | ((size_t)PTYPE_SOCKET << MESSAGE_TYPE_SHIFT);

    if (ztask_context_push((uint32_t)result->opaque, &message)) {
        // todo: report somewhere to close socket
        // don't call ztask_socket_close here (It will block mainloop)
        ztask_free(sm->buffer);
        ztask_free(sm);
    }
}

int
ztask_socket_poll() {
    if (socket_server_poll(SOCKET_SERVER) > 0) {
        return -1;
    }
    return 0;
}

static int
check_wsz(struct ztask_context *ctx, int id, void *buffer, int64_t wsz) {
    if (wsz < 0) {
        ztask_free(buffer);
        return -1;
    }
    else if (wsz > 1024 * 1024) {
        int kb4 = (int)(wsz / 1024 / 4);
        if (kb4 % 256 == 0) {
            ztask_error(ctx, "%d Mb bytes on socket %d need to send out", (int)(wsz / (1024 * 1024)), id);
        }
    }
    return 0;
}

int
ztask_socket_send(struct ztask_context *ctx, int id, void *buffer, int sz) {
    int64_t wsz = socket_server_send(SOCKET_SERVER, id, buffer, sz);
    return check_wsz(ctx, id, buffer, wsz);
}

int
ztask_socket_send_lowpriority(struct ztask_context *ctx, int id, void *buffer, int sz) {
    return socket_server_send_lowpriority(SOCKET_SERVER, id, buffer, sz);
}

int
ztask_socket_listen(struct ztask_context *ctx, int session, const char *host, int port, int backlog) {
    uint32_t source = ztask_context_handle(ctx);
    return socket_server_listen(SOCKET_SERVER, source, session, host, port, backlog);
}

int
ztask_socket_connect(struct ztask_context *ctx, int session, const char *host, int port) {
    uint32_t source = ztask_context_handle(ctx);
    return socket_server_connect(SOCKET_SERVER, source, session, host, port);
}

int
ztask_socket_bind(struct ztask_context *ctx, int fd) {
    uint32_t source = ztask_context_handle(ctx);
    return socket_server_bind(SOCKET_SERVER, source, fd);
}

void
ztask_socket_close(struct ztask_context *ctx, int id) {
    uint32_t source = ztask_context_handle(ctx);
    socket_server_close(SOCKET_SERVER, source, id);
}

void
ztask_socket_shutdown(struct ztask_context *ctx, int id) {
    uint32_t source = ztask_context_handle(ctx);
    socket_server_shutdown(SOCKET_SERVER, source, id);
}

void
ztask_socket_start(struct ztask_context *ctx, int session, int id) {
    uint32_t source = ztask_context_handle(ctx);
    socket_server_start(SOCKET_SERVER, source,session, id);
}

void
ztask_socket_nodelay(struct ztask_context *ctx, int id) {
    socket_server_nodelay(SOCKET_SERVER, id);
}

int
ztask_socket_udp(struct ztask_context *ctx, const char * addr, int port) {
    uint32_t source = ztask_context_handle(ctx);
    return socket_server_udp(SOCKET_SERVER, source, addr, port);
}

int
ztask_socket_udp_connect(struct ztask_context *ctx, int id, const char * addr, int port) {
    return socket_server_udp_connect(SOCKET_SERVER, id, addr, port);
}

int
ztask_socket_udp_send(struct ztask_context *ctx, int id, const char * address, const void *buffer, int sz) {
    int64_t wsz = socket_server_udp_send(SOCKET_SERVER, id, (const struct socket_udp_address *)address, buffer, sz);
    return check_wsz(ctx, id, (void *)buffer, wsz);
}

const char *
ztask_socket_udp_address(struct ztask_socket_message *msg, int *addrsz) {
    if (msg->type != ZTASK_SOCKET_TYPE_UDP) {
        return NULL;
    }
    struct socket_message sm;
    sm.id = msg->id;
    sm.opaque = 0;
    sm.ud = msg->ud;
    sm.data = msg->buffer;
    return (const char *)socket_server_udp_address(SOCKET_SERVER, &sm, addrsz);
}
