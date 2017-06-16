#ifndef BOTCOT_HEADER
#define BOTCOT_HEADER

#include <stddef.h>
#include <assert.h>
#include <math.h>

#define COROUTINE_DEAD     0
#define COROUTINE_READY    1
#define COROUTINE_RUNNING  2
#define COROUTINE_SUSPEND  3
#define COROUTINE_END      4

typedef void* fcontext_t;//协程上下文
typedef void(*fcontext_cb)(struct coroutine_s*);
//协程结构
typedef struct coroutine_s {
    fcontext_t nfc; //新协程
    fcontext_t ofc; //旧协程
    int status;     //执行状态
    void* sptr;     //栈数据指针
    size_t ssize;   //栈大小
    fcontext_cb cb; //用户回调
    //用户封装参数
    struct ztask_context * context;
    const void * msg;
    size_t sz;
}coroutine_t;

//创建一个协程
coroutine_t *coroutine_new(fcontext_cb);
//返回当前协程是否执行完毕
int coroutine_resume(coroutine_t *c);
void coroutine_yield();
coroutine_t *coroutine_running();
#endif // #ifndef BOTCOT_HEADER
