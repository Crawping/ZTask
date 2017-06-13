#include "ztask.h"
#include "coroutine.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct snc {
    struct ztask_context * ctx;
    ztask_cb _cb;


};
//转发callback
static int _cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {



}

static int init_cb(struct snc *l, struct ztask_context *ctx, struct ztask_snc * args, size_t sz) {
    if (sizeof(struct ztask_snc) != sz)
        return -1;
    l->_cb = args->_cb;
    if (!args->_init_cb)
        return -1;
    int err = args->_init_cb(ctx, args, sz);
    if (err)
        return 1;
    ztask_callback(ctx, l, _cb);
    return 0;
}
//此回调保证在业务线程被调用
static int launch_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    assert(type == 0 && session == 0);
    struct snlua *l = ud;
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
