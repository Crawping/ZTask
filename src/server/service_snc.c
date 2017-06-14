#include "ztask.h"
#include "coroutine.h"
#include "../ztask_server.h"
#include "../socket_server.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct snc {
    struct ztask_context * ctx;
    ztask_cb _cb;
    void *ud;

};
//封装异步操作
int ztask_snc_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog) {
    int ret = ztask_socket_listen(ctx, ztask_coroutine_cur(NULL), host, port, backlog);
    int cur = ztask_coroutine_cur(NULL);
    ztask_coroutine_cur(ztask_coroutine_main());
    //连接是个异步调用,所以切出当前协程
    coroutine_yield(ztask_coroutine_main(),cur);
    return ret;
}
void ztask_snc_socket_start(struct ztask_context *ctx, int fd) {
    ztask_socket_start(ctx, ztask_coroutine_cur(NULL), fd);
    int cur = ztask_coroutine_cur(NULL);
    ztask_coroutine_cur(ztask_coroutine_main());
    //连接是个异步调用,所以切出当前协程
    coroutine_yield(ztask_coroutine_main(), cur);
    int a=1;
    return;
}


//转发callback
static int _cb(struct ztask_context * context, struct snc *l, int type, int session, uint32_t source, const void * msg, size_t sz) {
    //切回请求的协程
    ztask_coroutine_cur(session);
    coroutine_resume(ztask_coroutine_main(), session);
    //l->_cb(context, l->ud, type, session, source, msg, sz);
}


void _func(void *context, void *ud, int type, int session, uint32_t source, struct ztask_snc * args, size_t sz) {
    return args->_init_cb(context, NULL, NULL);
}
static int init_cb(struct snc *l, struct ztask_context *ctx, struct ztask_snc * args, size_t sz) {
    if (sizeof(struct ztask_snc) != sz)
        return -1;
    l->_cb = args->_cb;
    if (!args->_init_cb)
        return -1;
    //设置回调函数
    ztask_callback(ctx, l, _cb);
    
    //coenv_t s = ztask_current_coroutine();
    //创建一个协程,用来执行初始化操作,因为初始化过程不能保证没有同步操作
    cort_t id = coroutine_new(ztask_coroutine_main(), _func, ctx, l, NULL, NULL, NULL, args, sz);
    ztask_coroutine_cur(id);
    //执行协程,会切出当前流程
    void *ud = coroutine_resume(ztask_coroutine_main(), id);
    //协程执完毕,切回当前流程
    //if (!ud)
    //    return -1;
    //l->ud = ud;
    return 0;
}
//此回调保证在业务线程被调用
static int launch_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    assert(type == 0 && session == 0);
    struct snc *l = ud;
    ztask_callback(context, NULL, NULL);
    int err = init_cb(l, context, msg, sz);
    if (err) {
        ztask_command(context, "EXIT", NULL);
    }

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
