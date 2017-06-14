#ifndef _COROUTINE_H_
#define _COROUTINE_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ztask_realloc realloc
#define ztask_malloc malloc
#define ztask_free free

    //协程句柄
    typedef struct coroutine * cort_t;

    //安装主协程
    cort_t co_new_main();
    //卸载主协程
    void co_delete_main(cort_t co);
    //创建一个协程
    typedef void *(*coroutine_func)(void *context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz);
    cort_t coroutine_new(cort_t main, coroutine_func, void *context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz);
    //启动协程
    void *coroutine_resume(cort_t main, cort_t id);
    //挂起当前协程
    void *coroutine_yield(cort_t main,cort_t co);

#ifdef __cplusplus
}
#endif

#endif
