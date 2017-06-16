#include "ztask.h"
#include "ztask_server.h"
#include "socket_server.h"
#include <uv.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "coroutine.h"

#include <assert.h>
#include <math.h>
#include <string.h>


struct snc {
    struct ztask_context * ctx;
    ztask_cb _cb;
    void *ud;

};
//协程参数
struct ztask_context_param {
    fcontext_t nfc;//当前协程
    fcontext_t ofc;
    struct ztask_context * context;
    const void * msg;
    size_t sz;
};

static uv_key_t *cur_key = NULL;

fcontext_t fcm, fc1;

void f1(int* a) {
    printf("f1: yild 0\n");
    jump_fcontext(&fc1, fcm, 0, 0);
    printf("f1: yild 1\n");
    jump_fcontext(&fc1, fcm, 0, 0);

    jump_fcontext(&fc1, fcm, 0, 0);
}

#define STACK_SIZE 4096

void test() {
    printf("Started\n");
    char stack[STACK_SIZE];
    fc1 = make_fcontext(stack, STACK_SIZE, f1);
    jump_fcontext(&fcm, fc1, 0, 0);
    printf("f1: res\n");

    jump_fcontext(&fcm, fc1, 0, 0);
    printf("f1: res\n");

    jump_fcontext(&fcm, fc1, 0, 0);
    printf("f1: res\n");
    printf("OK\n");

}




//封装异步操作
int ztask_snc_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog) {
    struct ztask_context_param *cur = uv_key_get(cur_key);
    int ret = ztask_socket_listen(ctx, cur, host, port, backlog);
    //连接是个异步调用,所以切出当前协程
    jump_fcontext(&cur->nfc,cur->ofc, NULL,NULL);
    return ret;
}
void ztask_snc_socket_start(struct ztask_context *ctx, int fd) {
    struct ztask_context_param *cur = uv_key_get(cur_key);
    ztask_socket_start(ctx, cur, fd);
    //连接是个异步调用,所以切出当前协程
    jump_fcontext(&cur->nfc, cur->ofc, NULL, NULL);
}

//转发callback
static int _cb(struct ztask_context * context, struct snc *l, int type, int session, uint32_t source, const void * msg, size_t sz) {
    //切回请求的协程
    struct ztask_context_param *cur = session;
    uv_key_set(cur_key, cur);
    jump_fcontext(&cur->ofc,cur->nfc, NULL,NULL);
}


static void _init_func(void *t) {
    struct ztask_context_param * param = t;
    uv_key_set(cur_key, param);
    int ret = ((ztask_snc_init_cb)param->msg)(param->context, NULL, NULL);
    jump_fcontext(&param->nfc, param->ofc, 0, 0);
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
    test();
    //创建协程执行init
    fcontext_stack_t s = create_fcontext_stack(0);
    struct ztask_context_param *param=malloc(sizeof(struct ztask_context_param));
    param->ofc = NULL;
    param->nfc = make_fcontext(s.sptr, s.ssize, _init_func);
    param->context = context;
    param->msg = args->_init_cb;
    param->sz = NULL;
    //struct fcontext_transfer_t t = 
    jump_fcontext(&param->ofc, param->nfc, param, 0);
    int err = 0;//t.data;
    //释放协程栈
    //destroy_fcontext_stack(&s);
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
    if (!cur_key) {
        cur_key = malloc(sizeof(uv_key_t));
        uv_key_create(cur_key);
    }
    return l;
}

void snc_release(struct snc *l) {

    ztask_free(l);
}

void snc_signal(struct snc *l, int signal) {

}

