#include "ztask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"
#include "ztask_server.h"
static int client_cb(struct ztask_context * ctx, struct snc *l, int type, int session, uint32_t source, const void * msg, size_t sz) {


    return 0;
}
static int client_init(struct ztask_context * ctx, const void * msg, size_t sz) {
    //int id=coroutine_current(ztask_current_coroutine());
    //ztask_socket_connect(ctx, 1, "127.0.0.1", 1234);
    ////连接是个异步调用,所以切出当前协程
    //coroutine_yield(ztask_current_coroutine());

    return 1;
}


static int server_cb(struct ztask_context * ctx, struct snc *l, int type, int session, uint32_t source, const void * msg, size_t sz) {


    return 0;
}
static int server_init(struct ztask_context * ctx, const void * msg, size_t sz) {
    //绑定端口,协程同步操作
    int fd = ztask_snc_socket_listen(ctx, "0.0.0.0", 1234, 10);
    //开始监听
    ztask_snc_socket_start(ctx, fd);
    //启动客户端
    struct ztask_snc client;
    client._cb = client_cb;
    client._init_cb = client_init;
    ztask_context_new("snc", &client, sizeof(struct ztask_snc));
    return 1;
}

int main(int argc, char *argv[]) {
    ztask_init();
    struct ztask_config config = { 0 };
    config.thread =  1;
    config.module_path = "./?.dll";
    config.harbor = 1;
    config.daemon = NULL;
    config.logger =NULL;
    config.logservice = "logger";
    config.profile = 1;
    config.bootstrap = "snc";
    struct ztask_snc boot;
    boot._cb = server_cb;
    boot._init_cb = server_init;
    config.bootstrap_parm = &boot;
    config.bootstrap_parm_sz = sizeof(struct ztask_snc);


    ztask_start(&config);
    ztask_uninit();
    return 0;
}
