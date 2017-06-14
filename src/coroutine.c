#include "coroutine.h"
//#include "ztask.h"
#if defined(_WIN32) || defined(_WIN64)
#define _WIN32_WINNT 0x0501
#include <windows.h>
#else
#include "coroutine.h"
#include <ucontext.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#define STACK_SIZE (64<<10)

enum
{
    COROUTINE_NONE = 0,
    COROUTINE_SUSPEND,
    COROUTINE_RUNNING,
    COROUTINE_END,
};
//单个协程
struct coroutine
{
    int state;
    coroutine_func func;
    void *result;
    void *context;
    void *ud;
    int type;
    int session;
    uint32_t source;
    const void * msg;
    size_t sz;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE fiber;
#else
    char *stack;
    int stacksize;
    ucontext_t uctx;
#endif
    struct coroutine *main;
};
typedef struct coroutine * cort_t;


#if defined(_WIN32) || defined(_WIN64)
static void WINAPI _proxyfunc(void *p)
{
    cort_t co = (cort_t)p;
#else
static void _proxyfunc(uint32_t low32, uint32_t hi32)
{
    uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
    cort_t co = (cort_t)ptr;
#endif
    co->result = co->func(co->context, co->ud, co->type, co->session, co->source, co->msg, co->sz);
    co->state = COROUTINE_END;
#if defined(_WIN32) || defined(_WIN64)
    SwitchToFiber(co->main->fiber);
#endif
}

static cort_t co_new(cort_t main,coroutine_func func, void *context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz)
{
    struct coroutine *co = ztask_malloc(sizeof(*co));
    co->state = COROUTINE_SUSPEND;
    co->func = func;
    co->result = NULL;
    co->context = context;
    co->ud = ud;
    co->type = type;
    co->session = session;
    co->source = source;
    co->msg = msg;
    co->sz = sz;
    co->main = main;
#if defined(_WIN32) || defined(_WIN64)
    co->fiber = CreateFiber(0, _proxyfunc, co);
#else
    co->stacksize = STACK_SIZE;
    co->stack = ztask_malloc(co->stacksize);

    getcontext(&co->uctx);
    co->uctx.uc_stack.ss_sp = co->stack;
    co->uctx.uc_stack.ss_size = co->stacksize;
    co->uctx.uc_link = &main->uctx;

    uintptr_t ptr = (uintptr_t)co;
    makecontext(&co->uctx, (void(*)(void))_proxyfunc, 2, (uint32_t)ptr, (uint32_t)(ptr >> 32));
#endif

    return co;
}

static void co_delete(cort_t co)
{
#if defined(_WIN32) || defined(_WIN64)
    DeleteFiber(co->fiber);
#else
    free(co->stack);
#endif
    ztask_free(co);
}

static void co_switch(cort_t from, cort_t to)
{
#if defined(_WIN32) || defined(_WIN64)
    SwitchToFiber(to->fiber);
#else
    swapcontext(&from->uctx, &to->uctx);
#endif
}


//安装主协程
cort_t co_new_main()
{
    struct coroutine *co = ztask_malloc(sizeof(*co));
    co->state = COROUTINE_RUNNING;
    co->func = NULL;
    co->result = NULL;
    co->context = NULL;

#if defined(_WIN32) || defined(_WIN64)
    co->fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
#endif

    return co;
}
//卸载主协程
void co_delete_main(cort_t co)
{
#if defined(_WIN32) || defined(_WIN64)
    ConvertFiberToThread();
#endif
    ztask_free(co);
}

//创建一个协程
cort_t coroutine_new(cort_t main,coroutine_func func, void *context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz)
{
    return co_new(main, func, context, ud, type, session, source, msg, sz);
}
//启动协程
void *coroutine_resume(cort_t main, cort_t co)
{
    if (co && co->state == COROUTINE_SUSPEND)
    {
        co->state = COROUTINE_RUNNING;
        co_switch(main, co);
        void *ret = co->result;
        if (co->state == COROUTINE_END)
        {
            co_delete(co);
        }
        return ret;
    }
    return NULL;
}
//挂起当前协程
void *coroutine_yield(cort_t main, cort_t co)
{
    if (co && co->state == COROUTINE_RUNNING)
    {
        co->state = COROUTINE_SUSPEND;
        co_switch(co, main);
        return co->result;
    }
    return NULL;
}
