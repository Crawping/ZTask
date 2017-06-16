#include "ztask.h"
#include "ztask_start.h"
#include "ztask_server.h"
#include "ztask_mq.h"
#include "ztask_handle.h"
#include "ztask_module.h"
#include "ztask_timer.h"
#include "ztask_monitor.h"
#include "ztask_socket.h"
#include "ztask_daemon.h"
#include "ztask_harbor.h"
#include "atomic.h"
#include <uv.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <malloc.h>

#include "server/service_snc.h"
#include "server/service_logger.h"
#include "server/service_snlua.h"
#include "server/service_harbor.h"
//监视器
struct monitor {
    int count;
    struct ztask_monitor ** m;
    uv_cond_t cond;
    uv_mutex_t mutex;
    int sleep;
    int quit;
};
//线程参数
struct worker_parm {
    struct monitor *m;
    int id;
    int weight;
};

static int SIG = 0;
static int thread_worker_num = 0;//业务线程数
static int thread_ready_num = 0;//已就绪线程数
static uv_thread_t *pid = NULL;//线程句柄数组
static struct monitor *m = NULL;//监视器数组
#define CHECK_ABORT if (ztask_context_total()==0) break;

static void handle_hup(int signal) {
    if (signal == SIGHUP) {
        SIG = 1;
    }
}

static void create_thread(uv_thread_t *thread, uv_thread_cb start_routine, void *arg) {
    if (uv_thread_create(thread, start_routine, arg)) {
        fprintf(stderr, "Create thread failed");
        exit(1);
    }
}

static void wakeup(struct monitor *m, int busy) {
    if (m->sleep >= m->count - busy) {
        // signal sleep worker, "spurious wakeup" is harmless
        uv_cond_signal(&m->cond);
    }
}

static void signal_hup() {
    // make log file reopen

    struct ztask_message smsg;
    smsg.source = 0;
    smsg.session = 0;
    smsg.data = NULL;
    smsg.sz = (size_t)PTYPE_SYSTEM << MESSAGE_TYPE_SHIFT;
    uint32_t logger = ztask_handle_findname("logger");
    if (logger) {
        ztask_context_push(logger, &smsg);
    }
}

static void free_monitor(struct monitor *m) {
    int i;
    int n = m->count;
    for (i = 0; i < n; i++) {
        ztask_monitor_delete(m->m[i]);
    }
    uv_mutex_destroy(&m->mutex);
    uv_cond_destroy(&m->cond);
    ztask_free(m->m);
    ztask_free(m);
}
//io线程
static void thread_socket(void *p) {
    ztask_initthread(THREAD_SOCKET);
    ATOM_INC(&thread_ready_num);
    for (;;) {
        int r = ztask_socket_poll();
        if (r == 0)
            break;
        if (r < 0) {
            CHECK_ABORT
                continue;
        }
        wakeup(m, 0);
    }
}
//监控线程
static void thread_monitor(void *p) {
    int i;
    int n = m->count;
    ztask_initthread(THREAD_MONITOR);
    ATOM_INC(&thread_ready_num);
    for (;;) {
        CHECK_ABORT
            for (i = 0; i < n; i++) {
                ztask_monitor_check(m->m[i]);
            }
        for (i = 0; i < 5; i++) {
            CHECK_ABORT
#if (defined(_WIN32) || defined(_WIN64))
                Sleep(1000);
#else
                sleep(1);
#endif
        }
    }
}
//时钟线程
static void thread_timer(void *p) {
    ztask_initthread(THREAD_TIMER);
    ATOM_INC(&thread_ready_num);
    for (;;) {
        ztask_updatetime();
        CHECK_ABORT
            wakeup(m, m->count - 1);
#if (defined(_WIN32) || defined(_WIN64))
        Sleep(25);
#else
        usleep(2500);
#endif
        if (SIG) {
            signal_hup();
            SIG = 0;
        }
    }
    // wakeup socket thread
    ztask_socket_exit();
    // wakeup all worker thread
    uv_mutex_lock(&m->mutex);
    m->quit = 1;
    uv_cond_broadcast(&m->cond);
    uv_mutex_unlock(&m->mutex);
    return;
}
//业务线程
static void thread_worker(void *p) {
    struct worker_parm *wp = p;
    int id = wp->id;
    int weight = wp->weight;
    struct ztask_monitor *sm = m->m[id];
    struct message_queue * q = NULL;
    ztask_initthread(THREAD_WORKER);
    ATOM_INC(&thread_ready_num);
    while (!m->quit) {
        q = ztask_context_message_dispatch(sm, q, weight);
        if (q == NULL) {
            uv_mutex_lock(&m->mutex);
            ++m->sleep;
            // "spurious wakeup" is harmless,
            // because ztask_context_message_dispatch() can be call at any time.
            if (!m->quit)
                uv_cond_wait(&m->cond, &m->mutex);
            --m->sleep;
            uv_mutex_unlock(&m->mutex);
        }
    }
    return;
}

static void start(int thread) {
    pid = ztask_malloc((thread + 3) * sizeof(*pid));
    thread_worker_num = thread;
    m = ztask_malloc(sizeof(*m));
    memset(m, 0, sizeof(*m));
    m->count = thread;
    m->sleep = 0;

    m->m = ztask_malloc(thread * sizeof(struct ztask_monitor *));
    int i;
    for (i = 0; i < thread; i++) {
        m->m[i] = ztask_monitor_new();
    }
    if (uv_mutex_init(&m->mutex)) {
        fprintf(stderr, "Init mutex error");
        exit(1);
    }
    if (uv_cond_init(&m->cond)) {
        fprintf(stderr, "Init cond error");
        exit(1);
    }

    uv_thread_create(&pid[0], thread_monitor, m);
    uv_thread_create(&pid[1], thread_timer, m);
    uv_thread_create(&pid[2], thread_socket, m);

    static int weight[] = {
        -1, -1, -1, -1, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3, };
    struct worker_parm *wp = ztask_malloc(thread * sizeof(*wp));
    //启动业务线程
    for (i = 0; i < thread; i++) {
        wp[i].m = m;
        wp[i].id = i;
        if (i < sizeof(weight) / sizeof(weight[0])) {
            wp[i].weight = weight[i];
        }
        else {
            wp[i].weight = 0;
        }
        uv_thread_create(&pid[i + 3], thread_worker, &wp[i]);
    }
    //等待线程就绪
    while (thread_ready_num!=(thread_worker_num+3))
    {
#if (defined(_WIN32) || defined(_WIN64))
        Sleep(25);
#else
        usleep(2500);
#endif
    }
}

void ztask_join() {
    for (int i = 0; i < thread_worker_num + 3; i++) {
        uv_thread_join(&pid[i]);
    }
    free_monitor(m);
    ztask_free(pid);
    ztask_free(m);
}
//注册预置模块
static void ztask_module_reg() {
    struct ztask_module *mod = NULL;
    mod = malloc(sizeof(*mod));
    mod->init = snc_init;
    mod->release = snc_release;
    mod->create = snc_create;
    mod->name = "snc";
    mod->module = NULL;
    ztask_module_insert(mod);

    mod = malloc(sizeof(*mod));
    mod->init = logger_init;
    mod->release = logger_release;
    mod->create = logger_create;
    mod->name = "logger";
    mod->module = NULL;
    ztask_module_insert(mod);

    mod = malloc(sizeof(*mod));
    mod->init = snlua_init;
    mod->release = snlua_release;
    mod->create = snlua_create;
    mod->signal = snlua_signal;
    mod->name = "snlua";
    mod->module = NULL;
    ztask_module_insert(mod);

    mod = malloc(sizeof(*mod));
    mod->init = harbor_init;
    mod->release = harbor_release;
    mod->create = harbor_create;
    mod->name = "harbor";
    mod->module = NULL;
    ztask_module_insert(mod);
}

ZTASK_EXTERN void ztask_start(struct ztask_config * config) {
    // register SIGHUP for log file reopen
    //struct sigaction sa;
    //sa.sa_handler = &handle_hup;
    //sa.sa_flags = SA_RESTART;
    //sigfillset(&sa.sa_mask);
    //sigaction(SIGHUP, &sa, NULL);

    //检查是否以守护进程方式启动,win暂不支持
    if (config->daemon) {
        if (daemon_init(config->daemon)) {
            exit(1);
        }
    }
    //初始化节点管理器
    ztask_harbor_init(config->harbor);
    //初始化句柄管理器
    ztask_handle_init(config->harbor);
    //初始化队列
    ztask_mq_init();
    //初始化模块管理器,指定查找路径
    ztask_module_init(config->module_path);
    //注册默认模块
    ztask_module_reg();
    //初始化时钟
    ztask_timer_init();
    //初始化io
    ztask_socket_init();
    ztask_profile_enable(config->profile);
    //查找日志服务
    struct ztask_context *logger = ztask_context_new(config->logservice, config->logger, config->logger ? strlen(config->logger) : NULL);
    if (logger == NULL) {
        fprintf(stderr, "Can't launch %s service\n", config->logservice);
        exit(1);
    }

    //启动线程
    start(config->thread);
    //自举
    struct ztask_context *ctx = ztask_context_new(config->bootstrap, config->bootstrap_parm, config->bootstrap_parm_sz);
    if (ctx == NULL) {
        ztask_error(NULL, "Bootstrap error : %s\n", config->bootstrap);
        ztask_context_dispatchall(logger);
        exit(1);
    }


    // harbor_exit may call socket send, so it should exit before socket_free
    //ztask_harbor_exit();
    //ztask_socket_free();
    ////退出守护进程,win暂不支持
    //if (config->daemon) {
    //    daemon_exit(config->daemon);
    //}
}


