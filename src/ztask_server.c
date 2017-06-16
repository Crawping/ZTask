#include "ztask.h"

#include "coroutine.h"
#include "ztask_start.h"
#include "ztask_server.h"
#include "ztask_module.h"
#include "ztask_handle.h"
#include "ztask_mq.h"
#include "ztask_timer.h"
#include "ztask_harbor.h"
#include "ztask_env.h"
#include "ztask_monitor.h"
#include "ztask_log.h"
#include "ztask_timer.h"
#include "spinlock.h"
#include "atomic.h"

#include <uv.h>

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <malloc.h>

#ifdef CALLING_CHECK

#define CHECKCALLING_BEGIN(ctx) if (!(spinlock_trylock(&ctx->calling))) { assert(0); }
#define CHECKCALLING_END(ctx) spinlock_unlock(&ctx->calling);
#define CHECKCALLING_INIT(ctx) spinlock_init(&ctx->calling);
#define CHECKCALLING_DESTROY(ctx) spinlock_destroy(&ctx->calling);
#define CHECKCALLING_DECL struct spinlock calling;

#else

#define CHECKCALLING_BEGIN(ctx)
#define CHECKCALLING_END(ctx)
#define CHECKCALLING_INIT(ctx)
#define CHECKCALLING_DESTROY(ctx)
#define CHECKCALLING_DECL

#endif

struct ztask_context {
    void * instance;
    struct ztask_module * mod;
    void * cb_ud;
    ztask_cb cb;
    struct message_queue *queue;
    FILE * logfile;
    uint64_t cpu_cost;	// in microsec
    uint64_t cpu_start;	// in microsec
    char result[32];
    uint32_t handle;
    int session_id;
    int ref;
    int message_count;
    bool init;
    bool endless;
    bool profile;

    CHECKCALLING_DECL
};

struct ztask_node {
    int total;
    int init;
    uint32_t monitor_exit;
    uv_key_t handle_key;
    bool profile;   // default is off
};

static struct ztask_node G_NODE;

int ztask_context_total() {
    return G_NODE.total;
}

static void context_inc() {
    ATOM_INC(&G_NODE.total);
}

static void context_dec() {
    ATOM_DEC(&G_NODE.total);
}

uint32_t ztask_current_handle(void) {
    if (G_NODE.init) {
        void * handle = uv_key_get(&G_NODE.handle_key);
        return (uint32_t)(uintptr_t)handle;
    }
    else {
        uint32_t v = (uint32_t)(-THREAD_MAIN);
        return v;
    }
}

static void id_to_hex(char * str, uint32_t id) {
    int i;
    static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    str[0] = ':';
    for (i = 0; i < 8; i++) {
        str[i + 1] = hex[(id >> ((7 - i) * 4)) & 0xf];
    }
    str[9] = '\0';
}

struct drop_t {
    uint32_t handle;
};

static void
drop_message(struct ztask_message *msg, void *ud) {
    struct drop_t *d = ud;
    ztask_free(msg->data);
    uint32_t source = d->handle;
    assert(source);
    // report error to the message source
    ztask_send(NULL, source, msg->source, PTYPE_ERROR, 0, NULL, 0);
}

struct ztask_context *
    ztask_context_new(const char * name, const char *param, const size_t sz) {
    struct ztask_module * mod = ztask_module_query(name);

    if (mod == NULL)
        return NULL;

    void *inst = ztask_module_instance_create(mod);
    if (inst == NULL)
        return NULL;
    struct ztask_context * ctx = ztask_malloc(sizeof(*ctx));
    CHECKCALLING_INIT(ctx);

    ctx->mod = mod;
    ctx->instance = inst;
    ctx->ref = 2;
    ctx->cb = NULL;
    ctx->cb_ud = NULL;
    ctx->session_id = 0;
    ctx->logfile = NULL;

    ctx->init = false;
    ctx->endless = false;

    ctx->cpu_cost = 0;
    ctx->cpu_start = 0;
    ctx->message_count = 0;
    ctx->profile = G_NODE.profile;
    // Should set to 0 first to avoid ztask_handle_retireall get an uninitialized handle
    ctx->handle = 0;
    ctx->handle = ztask_handle_register(ctx);
    struct message_queue * queue = ctx->queue = ztask_mq_create(ctx->handle);
    // init function maybe use ctx->handle, so it must init at last
    context_inc();

    CHECKCALLING_BEGIN(ctx);
    int r = ztask_module_instance_init(mod, inst, ctx, param, sz);
    CHECKCALLING_END(ctx);
    if (r == 0) {
        struct ztask_context * ret = ztask_context_release(ctx);
        if (ret) {
            ctx->init = true;
        }
        ztask_globalmq_push(queue);
        if (ret) {
            ztask_error(ret, "LAUNCH %s", name);
        }
        return ret;
    }
    else {
        ztask_error(ctx, "FAILED launch %s", name);
        uint32_t handle = ctx->handle;
        ztask_context_release(ctx);
        ztask_handle_retire(handle);
        struct drop_t d = { handle };
        ztask_mq_release(queue, drop_message, &d);
        return NULL;
    }
}

int
ztask_context_newsession(struct ztask_context *ctx) {
    // session always be a positive number
    int session = ++ctx->session_id;
    if (session <= 0) {
        ctx->session_id = 1;
        return 1;
    }
    return session;
}

void
ztask_context_grab(struct ztask_context *ctx) {
    ATOM_INC(&ctx->ref);
}

void
ztask_context_reserve(struct ztask_context *ctx) {
    ztask_context_grab(ctx);
    // don't count the context reserved, because ztask abort (the worker threads terminate) only when the total context is 0 .
    // the reserved context will be release at last.
    context_dec();
}

static void
delete_context(struct ztask_context *ctx) {
    if (ctx->logfile) {
        fclose(ctx->logfile);
    }
    ztask_module_instance_release(ctx->mod, ctx->instance);
    ztask_mq_mark_release(ctx->queue);
    CHECKCALLING_DESTROY(ctx);
    ztask_free(ctx);
    context_dec();
}

struct ztask_context *
    ztask_context_release(struct ztask_context *ctx) {
    if (ATOM_DEC(&ctx->ref) == 0) {
        delete_context(ctx);
        return NULL;
    }
    return ctx;
}

int
ztask_context_push(uint32_t handle, struct ztask_message *message) {
    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (ctx == NULL) {
        return -1;
    }
    ztask_mq_push(ctx->queue, message);
    ztask_context_release(ctx);

    return 0;
}

void
ztask_context_endless(uint32_t handle) {
    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (ctx == NULL) {
        return;
    }
    ctx->endless = true;
    ztask_context_release(ctx);
}

int
ztask_isremote(struct ztask_context * ctx, uint32_t handle, int * harbor) {
    int ret = ztask_harbor_message_isremote(handle);
    if (harbor) {
        *harbor = (int)(handle >> HANDLE_REMOTE_SHIFT);
    }
    return ret;
}

static void
dispatch_message(struct ztask_context *ctx, struct ztask_message *msg) {
    assert(ctx->init);
    CHECKCALLING_BEGIN(ctx);
    uv_key_set(&G_NODE.handle_key, (void *)(uintptr_t)(ctx->handle));
    int type = msg->sz >> MESSAGE_TYPE_SHIFT;
    size_t sz = msg->sz & MESSAGE_TYPE_MASK;
    if (ctx->logfile) {
        ztask_log_output(ctx->logfile, msg->source, type, msg->session, msg->data, sz);
    }
    ++ctx->message_count;
    int reserve_msg;
    if (ctx->profile) {
        ctx->cpu_start = ztask_thread_time();
        reserve_msg = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
        uint64_t cost_time = ztask_thread_time() - ctx->cpu_start;
        ctx->cpu_cost += cost_time;
    }
    else {
        reserve_msg = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
    }
    if (!reserve_msg) {
        ztask_free(msg->data);
    }
    CHECKCALLING_END(ctx);
}

void
ztask_context_dispatchall(struct ztask_context * ctx) {
    // for ztask_error
    struct ztask_message msg;
    struct message_queue *q = ctx->queue;
    while (!ztask_mq_pop(q, &msg)) {
        dispatch_message(ctx, &msg);
    }
}

struct message_queue *
    ztask_context_message_dispatch(struct ztask_monitor *sm, struct message_queue *q, int weight) {
    if (q == NULL) {
        q = ztask_globalmq_pop();
        if (q == NULL)
            return NULL;
    }

    uint32_t handle = ztask_mq_handle(q);

    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (ctx == NULL) {
        struct drop_t d = { handle };
        ztask_mq_release(q, drop_message, &d);
        return ztask_globalmq_pop();
    }

    int i, n = 1;
    struct ztask_message msg;

    for (i = 0; i < n; i++) {
        if (ztask_mq_pop(q, &msg)) {
            ztask_context_release(ctx);
            return ztask_globalmq_pop();
        }
        else if (i == 0 && weight >= 0) {
            n = ztask_mq_length(q);
            n >>= weight;
        }
        int overload = ztask_mq_overload(q);
        if (overload) {
            ztask_error(ctx, "May overload, message queue length = %d", overload);
        }

        ztask_monitor_trigger(sm, msg.source, handle);

        if (ctx->cb == NULL) {
            ztask_free(msg.data);
        }
        else {
            dispatch_message(ctx, &msg);
        }

        ztask_monitor_trigger(sm, 0, 0);
    }

    assert(q == ctx->queue);
    struct message_queue *nq = ztask_globalmq_pop();
    if (nq) {
        // If global mq is not empty , push q back, and return next queue (nq)
        // Else (global mq is empty or block, don't push q back, and return q again (for next dispatch)
        ztask_globalmq_push(q);
        q = nq;
    }
    ztask_context_release(ctx);

    return q;
}

static void
copy_name(char name[GLOBALNAME_LENGTH], const char * addr) {
    int i;
    for (i = 0; i < GLOBALNAME_LENGTH && addr[i]; i++) {
        name[i] = addr[i];
    }
    for (; i < GLOBALNAME_LENGTH; i++) {
        name[i] = '\0';
    }
}

uint32_t
ztask_queryname(struct ztask_context * context, const char * name) {
    switch (name[0]) {
    case ':':
        return strtoul(name + 1, NULL, 16);
    case '.':
        return ztask_handle_findname(name + 1);
    }
    ztask_error(context, "Don't support query global name %s", name);
    return 0;
}

static void
handle_exit(struct ztask_context * context, uint32_t handle) {
    if (handle == 0) {
        handle = context->handle;
        ztask_error(context, "KILL self");
    }
    else {
        ztask_error(context, "KILL :%0x", handle);
    }
    if (G_NODE.monitor_exit) {
        ztask_send(context, handle, G_NODE.monitor_exit, PTYPE_CLIENT, 0, NULL, 0);
    }
    ztask_handle_retire(handle);
}

// ztask command

struct command_func {
    const char *name;
    const char * (*func)(struct ztask_context * context, const char * param);
};

static const char *
cmd_timeout(struct ztask_context * context, const char * param) {
    char * session_ptr = NULL;
    int ti = strtol(param, &session_ptr, 10);
    int session = ztask_context_newsession(context);
    ztask_timeout(context->handle, ti, session);
    sprintf(context->result, "%d", session);
    return context->result;
}

static const char *
cmd_reg(struct ztask_context * context, const char * param) {
    if (param == NULL || param[0] == '\0') {
        sprintf(context->result, ":%x", context->handle);
        return context->result;
    }
    else if (param[0] == '.') {
        return ztask_handle_namehandle(context->handle, param + 1);
    }
    else {
        ztask_error(context, "Can't register global name %s in C", param);
        return NULL;
    }
}

static const char *
cmd_query(struct ztask_context * context, const char * param) {
    if (param[0] == '.') {
        uint32_t handle = ztask_handle_findname(param + 1);
        if (handle) {
            sprintf(context->result, ":%x", handle);
            return context->result;
        }
    }
    return NULL;
}

static const char *
cmd_name(struct ztask_context * context, const char * param) {
    size_t size = strlen(param);
    char *name = alloca(size + 1);
    char *handle = alloca(size + 1);
    sscanf(param, "%s %s", name, handle);
    if (handle[0] != ':') {
        return NULL;
    }
    uint32_t handle_id = strtoul(handle + 1, NULL, 16);
    if (handle_id == 0) {
        return NULL;
    }
    if (name[0] == '.') {
        return ztask_handle_namehandle(handle_id, name + 1);
    }
    else {
        ztask_error(context, "Can't set global name %s in C", name);
    }
    return NULL;
}

static const char *
cmd_exit(struct ztask_context * context, const char * param) {
    handle_exit(context, 0);
    return NULL;
}

static uint32_t
tohandle(struct ztask_context * context, const char * param) {
    uint32_t handle = 0;
    if (param[0] == ':') {
        handle = strtoul(param + 1, NULL, 16);
    }
    else if (param[0] == '.') {
        handle = ztask_handle_findname(param + 1);
    }
    else {
        ztask_error(context, "Can't convert %s to handle", param);
    }

    return handle;
}

static const char *
cmd_kill(struct ztask_context * context, const char * param) {
    uint32_t handle = tohandle(context, param);
    if (handle) {
        handle_exit(context, handle);
    }
    return NULL;
}

char *Test_strpbrk(const char *cs, const char *ct)
{
    const char *sc1 = cs, *sc2;

    for (; *sc1 != '\0'; ++sc1) {
        for (sc2 = ct; *sc2 != '\0'; ++sc2) {
            if (*sc1 == *sc2)
                return (char *)sc1;
        }
    }
    return NULL;
}
char *strsep(char **s, const char *ct)
{
    char *sbegin = *s;
    char *end;

    if (sbegin == NULL)
        return NULL;

    end = Test_strpbrk(sbegin, ct);
    if (end)
        *end++ = '\0';
    *s = end;
    return sbegin;
}

static const char *
cmd_launch(struct ztask_context * context, const char * param) {
    size_t sz = strlen(param);
    char *tmp = alloca(sz + 1);
    strcpy(tmp, param);
    char * args = tmp;
    char * mod = strsep(&args, " \t\r\n");
    args = strsep(&args, "\r\n");
    struct ztask_context * inst = ztask_context_new(mod, args, strlen(args));
    if (inst == NULL) {
        return NULL;
    }
    else {
        id_to_hex(context->result, inst->handle);
        return context->result;
    }
}

static const char *
cmd_getenv(struct ztask_context * context, const char * param) {
    return ztask_getenv(param);
}

static const char *
cmd_setenv(struct ztask_context * context, const char * param) {
    size_t sz = strlen(param);
    char *key = alloca(sz + 1);
    int i;
    for (i = 0; param[i] != ' ' && param[i]; i++) {
        key[i] = param[i];
    }
    if (param[i] == '\0')
        return NULL;

    key[i] = '\0';
    param += i + 1;

    ztask_setenv(key, param);
    return NULL;
}

static const char *
cmd_starttime(struct ztask_context * context, const char * param) {
    uint32_t sec = ztask_starttime();
    sprintf(context->result, "%u", sec);
    return context->result;
}

static const char *
cmd_abort(struct ztask_context * context, const char * param) {
    ztask_handle_retireall();
    return NULL;
}

static const char *
cmd_monitor(struct ztask_context * context, const char * param) {
    uint32_t handle = 0;
    if (param == NULL || param[0] == '\0') {
        if (G_NODE.monitor_exit) {
            // return current monitor serivce
            sprintf(context->result, ":%x", G_NODE.monitor_exit);
            return context->result;
        }
        return NULL;
    }
    else {
        handle = tohandle(context, param);
    }
    G_NODE.monitor_exit = handle;
    return NULL;
}

static const char *
cmd_stat(struct ztask_context * context, const char * param) {
    if (strcmp(param, "mqlen") == 0) {
        int len = ztask_mq_length(context->queue);
        sprintf(context->result, "%d", len);
    }
    else if (strcmp(param, "endless") == 0) {
        if (context->endless) {
            strcpy(context->result, "1");
            context->endless = false;
        }
        else {
            strcpy(context->result, "0");
        }
    }
    else if (strcmp(param, "cpu") == 0) {
        double t = (double)context->cpu_cost / 1000000.0;	// microsec
        sprintf(context->result, "%lf", t);
    }
    else if (strcmp(param, "time") == 0) {
        if (context->profile) {
            uint64_t ti = ztask_thread_time() - context->cpu_start;
            double t = (double)ti / 1000000.0;	// microsec
            sprintf(context->result, "%lf", t);
        }
        else {
            strcpy(context->result, "0");
        }
    }
    else if (strcmp(param, "message") == 0) {
        sprintf(context->result, "%d", context->message_count);
    }
    else {
        context->result[0] = '\0';
    }
    return context->result;
}

static const char *
cmd_logon(struct ztask_context * context, const char * param) {
    uint32_t handle = tohandle(context, param);
    if (handle == 0)
        return NULL;
    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (ctx == NULL)
        return NULL;
    FILE *f = NULL;
    FILE * lastf = ctx->logfile;
    if (lastf == NULL) {
        f = ztask_log_open(context, handle);
        if (f) {
            if (!ATOM_CAS_POINTER(&ctx->logfile, NULL, f)) {
                // logfile opens in other thread, close this one.
                fclose(f);
            }
        }
    }
    ztask_context_release(ctx);
    return NULL;
}

static const char *
cmd_logoff(struct ztask_context * context, const char * param) {
    uint32_t handle = tohandle(context, param);
    if (handle == 0)
        return NULL;
    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (ctx == NULL)
        return NULL;
    FILE * f = ctx->logfile;
    if (f) {
        // logfile may close in other thread
        if (ATOM_CAS_POINTER(&ctx->logfile, f, NULL)) {
            ztask_log_close(context, f, handle);
        }
    }
    ztask_context_release(ctx);
    return NULL;
}

static const char *
cmd_signal(struct ztask_context * context, const char * param) {
    uint32_t handle = tohandle(context, param);
    if (handle == 0)
        return NULL;
    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (ctx == NULL)
        return NULL;
    param = strchr(param, ' ');
    int sig = 0;
    if (param) {
        sig = strtol(param, NULL, 0);
    }
    // NOTICE: the signal function should be thread safe.
    ztask_module_instance_signal(ctx->mod, ctx->instance, sig);

    ztask_context_release(ctx);
    return NULL;
}

static struct command_func cmd_funcs[] = {
    { "TIMEOUT", cmd_timeout },
    { "REG", cmd_reg },
    { "QUERY", cmd_query },
    { "NAME", cmd_name },
    { "EXIT", cmd_exit },
    { "KILL", cmd_kill },
    { "LAUNCH", cmd_launch },
    { "GETENV", cmd_getenv },
    { "SETENV", cmd_setenv },
    { "STARTTIME", cmd_starttime },
    { "ABORT", cmd_abort },
    { "MONITOR", cmd_monitor },
    { "STAT", cmd_stat },
    { "LOGON", cmd_logon },
    { "LOGOFF", cmd_logoff },
    { "SIGNAL", cmd_signal },
    { NULL, NULL },
};

const char *
ztask_command(struct ztask_context * context, const char * cmd, const char * param) {
    struct command_func * method = &cmd_funcs[0];
    while (method->name) {
        if (strcmp(cmd, method->name) == 0) {
            return method->func(context, param);
        }
        ++method;
    }

    return NULL;
}

static void
_filter_args(struct ztask_context * context, int type, int *session, void ** data, size_t * sz) {
    int needcopy = !(type & PTYPE_TAG_DONTCOPY);
    int allocsession = type & PTYPE_TAG_ALLOCSESSION;
    type &= 0xff;

    if (allocsession) {
        assert(*session == 0);
        *session = ztask_context_newsession(context);
    }

    if (needcopy && *data) {
        char * msg = ztask_malloc(*sz + 1);
        memcpy(msg, *data, *sz);
        msg[*sz] = '\0';
        *data = msg;
    }

    *sz |= (size_t)type << MESSAGE_TYPE_SHIFT;
}

int
ztask_send(struct ztask_context * context, uint32_t source, uint32_t destination, int type, int session, void * data, size_t sz) {
    if ((sz & MESSAGE_TYPE_MASK) != sz) {
        ztask_error(context, "The message to %x is too large", destination);
        if (type & PTYPE_TAG_DONTCOPY) {
            ztask_free(data);
        }
        return -1;
    }
    _filter_args(context, type, &session, (void **)&data, &sz);

    if (source == 0) {
        source = context->handle;
    }

    if (destination == 0) {
        return session;
    }
    if (ztask_harbor_message_isremote(destination)) {
        struct remote_message * rmsg = ztask_malloc(sizeof(*rmsg));
        rmsg->destination.handle = destination;
        rmsg->message = data;
        rmsg->sz = sz;
        ztask_harbor_send(rmsg, source, session);
    }
    else {
        struct ztask_message smsg;
        smsg.source = source;
        smsg.session = session;
        smsg.data = data;
        smsg.sz = sz;

        if (ztask_context_push(destination, &smsg)) {
            ztask_free(data);
            return -1;
        }
    }
    return session;
}

int
ztask_sendname(struct ztask_context * context, uint32_t source, const char * addr, int type, int session, void * data, size_t sz) {
    if (source == 0) {
        source = context->handle;
    }
    uint32_t des = 0;
    if (addr[0] == ':') {
        des = strtoul(addr + 1, NULL, 16);
    }
    else if (addr[0] == '.') {
        des = ztask_handle_findname(addr + 1);
        if (des == 0) {
            if (type & PTYPE_TAG_DONTCOPY) {
                ztask_free(data);
            }
            return -1;
        }
    }
    else {
        _filter_args(context, type, &session, (void **)&data, &sz);

        struct remote_message * rmsg = ztask_malloc(sizeof(*rmsg));
        copy_name(rmsg->destination.name, addr);
        rmsg->destination.handle = 0;
        rmsg->message = data;
        rmsg->sz = sz;

        ztask_harbor_send(rmsg, source, session);
        return session;
    }

    return ztask_send(context, source, des, type, session, data, sz);
}

uint32_t
ztask_context_handle(struct ztask_context *ctx) {
    return ctx->handle;
}

void
ztask_callback(struct ztask_context * context, void *ud, ztask_cb cb) {
    context->cb = cb;
    context->cb_ud = ud;
}

void
ztask_context_send(struct ztask_context * ctx, void * msg, size_t sz, uint32_t source, int type, int session) {
    struct ztask_message smsg;
    smsg.source = source;
    smsg.session = session;
    smsg.data = msg;
    smsg.sz = sz | (size_t)type << MESSAGE_TYPE_SHIFT;

    ztask_mq_push(ctx->queue, &smsg);
}

void
ztask_globalinit(void) {
    G_NODE.total = 0;
    G_NODE.monitor_exit = 0;
    G_NODE.init = 1;
    if (uv_key_create(&G_NODE.handle_key)) {
        fprintf(stderr, "pthread_key_create failed");
        exit(1);
    }
    // set mainthread's key
    ztask_initthread(THREAD_MAIN);
}

void
ztask_globalexit(void) {
    uv_key_delete(&G_NODE.handle_key);
}

void
ztask_initthread(int m) {
    uintptr_t v = (uint32_t)(-m);
    uv_key_set(&G_NODE.handle_key, (void *)v);
}

void
ztask_profile_enable(int enable) {
    G_NODE.profile = (bool)enable;
}
