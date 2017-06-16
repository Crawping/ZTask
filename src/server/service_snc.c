#include "ztask.h"
#include "ztask_server.h"
#include "socket_server.h"
#include "coroutine.h"
#include <uv.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>


struct snc {
    struct ztask_context * ctx;
    ztask_cb _cb;
    void *ud;
};



//封装异步操作
int ztask_snc_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog) {
    coroutine_t *cur = coroutine_running();
    int ret = ztask_socket_listen(ctx, cur, host, port, backlog);
    //连接是个异步调用,所以切出当前协程
    coroutine_yield();
    return ret;
}
void ztask_snc_socket_start(struct ztask_context *ctx, int fd) {
    coroutine_t *cur = coroutine_running();
    ztask_socket_start(ctx, cur, fd);
    //连接是个异步调用,所以切出当前协程
    coroutine_yield();
}

//转发callback
static int _cb(struct ztask_context * context, struct snc *l, int type, int session, uint32_t source, const void * msg, size_t sz) {
    //切回请求的协程
    coroutine_resume(session);
}


static void _init_func(coroutine_t *c) {
    int err = ((ztask_snc_init_cb)c->msg)(c->context, NULL, NULL);
    //初始化流程正式完毕
    if (err) {
        ztask_command(c->context, "EXIT", NULL);
    }
}
//此回调保证在业务线程被调用
static int launch_cb(struct ztask_context * context, struct snc *l, int type, int session, uint32_t source, struct ztask_snc * args, size_t sz) {
    assert(type == 0 && session == 0);
    if (sizeof(struct ztask_snc) != sz)
        return -1;
    l->_cb = args->_cb;
    if (!args->_init_cb)
        return -1;
    //设置回调函数
    ztask_callback(context, l, _cb);

    //创建协程执行init
    coroutine_t * c = coroutine_new(_init_func);
    c->context = context;
    c->msg = args->_init_cb;
    c->sz = NULL;
    coroutine_resume(c);
    //返回不一定初始化成功可能是协程挂起
    return 0;
}

int snc_init(struct snc *l, struct ztask_context *ctx, const char * args, const size_t sz) {
    if (!args || !sz)
        return -1;
    char * tmp = ztask_malloc(sz);
    memcpy(tmp, args, sz);
    ztask_callback(ctx, l, launch_cb);
    uint32_t handle_id = ztask_context_handle(ctx);
    // it must be first message
    ztask_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY, 0, tmp, sz);
    return 0;
}

struct snc *snc_create(void) {
    struct snc * l = ztask_malloc(sizeof(*l));
    memset(l, 0, sizeof(*l));
    return l;
}

void snc_release(struct snc *l) {

    ztask_free(l);
}

void snc_signal(struct snc *l, int signal) {

}

