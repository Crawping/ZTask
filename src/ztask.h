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
��������������,��־����,bootstrap����
���������ģ��,��������ڵ��������治�ɼ�,��ͬʵ�ֵķ����ɶ�Ӧ��ģ�����,ģ��ʵ��Ӧ��Ϊ�м��,Ҳ�������ģ�鼴�������̬
���񴴽��ɹ�������ȫ��Ψһ��id��,��8λΪ�ڵ�id,��24λΪ����Ψһid
ÿ���������Լ���context
*/
struct ztask_context;
struct ztask_config {
    int thread;                 //ҵ���߳���
    int harbor;                 //�ڵ�id
    int profile;                //
    const char * daemon;        //�ػ�ģʽ,��unix
    const char * module_path;   //ģ�����·��
    const char * bootstrap;     //bootstrap����
    const char * bootstrap_parm;//bootstrap�������
    size_t bootstrap_parm_sz;//��������
    const char * logservice;    //��־����
    const char * logger;        //��־�ļ�,���������ӡ������̨
    
};
ZTASK_EXTERN void ztask_init();
ZTASK_EXTERN void ztask_uninit();
//����������
ZTASK_EXTERN void ztask_start(struct ztask_config * config);

//���������Ϣ
ZTASK_EXTERN void ztask_error(struct ztask_context * context, const char *msg, ...);
//���ÿ��ƺ���,һ����û�еײ���������Ľű���ģ��ʹ��
ZTASK_EXTERN const char * ztask_command(struct ztask_context * context, const char * cmd , const char * parm);
//����һ��������,��ͬ����
ZTASK_EXTERN struct ztask_context * ztask_context_new(const char * name, const char * parm, const size_t sz);
//�ù������Ĳ�ѯhandle
ZTASK_EXTERN uint32_t ztask_context_handle(struct ztask_context *);
//���Ʋ�ѯhandle
ZTASK_EXTERN uint32_t ztask_queryname(struct ztask_context * context, const char * name);
//ͨ��handle������Ϣ
ZTASK_EXTERN int ztask_send(struct ztask_context * context, uint32_t source, uint32_t destination , int type, int session, void * msg, size_t sz);
//ͨ�����Ʒ�����Ϣ
ZTASK_EXTERN int ztask_sendname(struct ztask_context * context, uint32_t source, const char * destination , int type, int session, void * msg, size_t sz);

//�Ƿ���Զ�̷���
ZTASK_EXTERN int ztask_isremote(struct ztask_context *, uint32_t handle, int * harbor);

//���ûص���ַ
typedef int (*ztask_cb)(struct ztask_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz);
ZTASK_EXTERN void ztask_callback(struct ztask_context * context, void *ud, ztask_cb cb);


//����̵߳�ǰ��handle
ZTASK_EXTERN uint32_t ztask_current_handle(void);
ZTASK_EXTERN uint64_t ztask_now(void);
#if defined(WIN32) || defined(WIN64)
ZTASK_EXTERN void usleep(uint32_t us);
ZTASK_EXTERN char *strsep(char **s, const char *ct);
#endif

//io���
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


/*ģ�����*/
//cģ����������

typedef int(*ztask_snc_init_cb)(struct ztask_context * context, const void * msg, size_t sz);
struct ztask_snc {
    ztask_cb _cb;
    ztask_snc_init_cb _init_cb;
};



/*�ڴ����*/
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
