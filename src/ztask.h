#ifndef ZTASK_H
#define ZTASK_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
# if defined(BUILDING)
/* Building shared library. */
#   define ZTASK_EXTERN __declspec(dllexport)
#   define ZTASK_MODULE __declspec(dllexport)
# elif defined(USING)
/* Using shared library. */
#   define ZTASK_EXTERN __declspec(dllimport)
#   define ZTASK_MODULE __declspec(dllimport)
# else
/* Building static library. */
#   define ZTASK_EXTERN /* nothing */
# endif
#elif __GNUC__ >= 4
# define ZTASK_EXTERN __attribute__((visibility("default")))
#else
# define ZTASK_EXTERN /* nothing */
#endif

#define PTYPE_TEXT 0
#define PTYPE_RESPONSE 1
#define PTYPE_MULTICAST 2
#define PTYPE_CLIENT 3
#define PTYPE_SYSTEM 4
#define PTYPE_HARBOR 5
#define PTYPE_SOCKET 6
#define PTYPE_ERROR 7	


#define PTYPE_TAG_DONTCOPY 0x10000
#define PTYPE_TAG_ALLOCSESSION 0x20000
/*
调度器启动流程,日志服务,bootstrap服务
服务寄生于模块,具体服务在调度器层面不可见,不同实现的服务由对应的模块加载,模块实现应该为中间件,也允许存在模块即服务的形态
服务创建成功后会分配全局唯一的id号,高8位为节点id,低24位为本地唯一id
每个服务都有自己的context
*/
struct ztask_context;
struct ztask_config {
    int thread;                 //业务线程数
    int harbor;                 //节点id
    int profile;                //
    const char * daemon;        //守护模式,仅unix
    const char * module_path;   //模块查找路径
    const char * bootstrap;     //bootstrap服务
    const char * bootstrap_parm;//bootstrap服务参数
    size_t bootstrap_parm_sz;//参数长度
    const char * logservice;    //日志服务
    const char * logger;        //日志文件,不定义则打印到控制台
    
};
ZTASK_EXTERN void ztask_init();
ZTASK_EXTERN void ztask_uninit();
//启动调度器
ZTASK_EXTERN void ztask_start(struct ztask_config * config);

//输出调试信息
ZTASK_EXTERN void ztask_error(struct ztask_context * context, const char *msg, ...);
//调用控制函数,一般由没有底层调用能力的脚本类模块使用
ZTASK_EXTERN const char * ztask_command(struct ztask_context * context, const char * cmd , const char * parm);
//创建一个上下文,等同服务
ZTASK_EXTERN struct ztask_context * ztask_context_new(const char * name, const char * parm, const size_t sz);
//用过上下文查询handle
ZTASK_EXTERN uint32_t ztask_context_handle(struct ztask_context *);
//名称查询handle
ZTASK_EXTERN uint32_t ztask_queryname(struct ztask_context * context, const char * name);
//通过handle发送消息
ZTASK_EXTERN int ztask_send(struct ztask_context * context, uint32_t source, uint32_t destination , int type, int session, void * msg, size_t sz);
//通过名称发送消息
ZTASK_EXTERN int ztask_sendname(struct ztask_context * context, uint32_t source, const char * destination , int type, int session, void * msg, size_t sz);

//是否是远程服务
ZTASK_EXTERN int ztask_isremote(struct ztask_context *, uint32_t handle, int * harbor);

//设置回调地址
typedef int (*ztask_cb)(struct ztask_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz);
ZTASK_EXTERN void ztask_callback(struct ztask_context * context, void *ud, ztask_cb cb);


//获得线程当前的handle
ZTASK_EXTERN uint32_t ztask_current_handle(void);
ZTASK_EXTERN uint64_t ztask_now(void);
#if defined(WIN32) || defined(WIN64)
ZTASK_EXTERN void usleep(uint32_t us);
ZTASK_EXTERN char *strsep(char **s, const char *ct);
#endif

//io相关
ZTASK_EXTERN int ztask_socket_send(struct ztask_context *ctx, int id, void *buffer, int sz);
ZTASK_EXTERN int ztask_socket_send_lowpriority(struct ztask_context *ctx, int id, void *buffer, int sz);
ZTASK_EXTERN int ztask_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog);
ZTASK_EXTERN int ztask_socket_connect(struct ztask_context *ctx, const char *host, int port);
ZTASK_EXTERN int ztask_socket_bind(struct ztask_context *ctx, int fd);
ZTASK_EXTERN void ztask_socket_close(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_shutdown(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_start(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_nodelay(struct ztask_context *ctx, int id);

ZTASK_EXTERN int ztask_socket_udp(struct ztask_context *ctx, const char * addr, int port);
ZTASK_EXTERN int ztask_socket_udp_connect(struct ztask_context *ctx, int id, const char * addr, int port);
ZTASK_EXTERN int ztask_socket_udp_send(struct ztask_context *ctx, int id, const char * address, const void *buffer, int sz);
ZTASK_EXTERN const char * ztask_socket_udp_address(struct ztask_socket_message *, int *addrsz);


/*模块相关*/
//c模块启动参数

typedef int(*ztask_snc_init_cb)(struct ztask_context * context, const void * msg, size_t sz);
struct ztask_snc {
    ztask_cb _cb;
    ztask_snc_init_cb _init_cb;
};



/*内存相关*/
#include <stddef.h>
#include <malloc.h>
#ifndef USE_JEMALLOC
#define ztask_malloc malloc
#define ztask_calloc calloc
#define ztask_realloc realloc
#define ztask_free free
#else
ZTASK_EXTERN void * ztask_malloc(size_t sz);
ZTASK_EXTERN void * ztask_calloc(size_t nmemb, size_t size);
ZTASK_EXTERN void * ztask_realloc(void *ptr, size_t size);
ZTASK_EXTERN void ztask_free(void *ptr);
#endif
ZTASK_EXTERN char * ztask_strdup(const char *str);
ZTASK_EXTERN void * ztask_lalloc(void *ptr, size_t osize, size_t nsize);	// use for lua
ZTASK_EXTERN void ztask_debug_memory(const char *info);	// for debug use, output current service memory to stderr
#endif
